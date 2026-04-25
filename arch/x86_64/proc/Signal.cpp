#include <ceryx/proc/Signal.hpp>
#include <ceryx/proc/Thread.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/proc/Scheduler.hpp>
#include <ceryx/proc/Reaper.hpp>
#include <ceryx/cpu/Idt.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>

namespace ceryx::proc {

void Signal::Send(Thread* thread, int sig) {
    if (!thread || sig < 1 || sig >= NSIG) return;
    thread->PendingSignals() |= (1ULL << (sig - 1));
}

void Signal::Raise(int sig) {
    Send(Scheduler::GetCurrentThread(), sig);
}

static void SetupFrame(cpu::InterruptFrame* frame, int sig, const sigaction& action) {
    // 1. Calculate new user stack pointer
    u64 rsp = frame->rsp;

    // Space for sigframe
    rsp -= sizeof(sigframe);
    
    // Align to 16 bytes (x86_64 ABI requirement)
    rsp &= ~15UL;
    
    // We need to be careful: this memory is in user-space.
    // For now, since we don't have a reliable copy_to_user yet, we'll cast.
    // FIXME: use safe memory access.
    sigframe* uframe = reinterpret_cast<sigframe*>(rsp);

    // 2. Build the frame
    FoundationKitMemory::MemoryZero(uframe, sizeof(sigframe));
    
    // Fill siginfo
    uframe->info.si_signo = sig;
    
    // Fill ucontext / mcontext
    auto& m = uframe->uc.uc_mcontext;
    m.r15 = frame->r15; m.r14 = frame->r14; m.r13 = frame->r13; m.r12 = frame->r12;
    m.r11 = frame->r11; m.r10 = frame->r10; m.r9  = frame->r9;  m.r8  = frame->r8;
    m.rbp = frame->rbp; m.rdi = frame->rdi; m.rsi = frame->rsi; m.rdx = frame->rdx;
    m.rcx = frame->rcx; m.rbx = frame->rbx; m.rax = frame->rax;
    m.rsp = frame->rsp;
    m.rip = frame->rip;
    m.eflags = frame->rflags;
    m.cs = frame->cs;

    // 3. Prepare registers for handler entry
    frame->rip = reinterpret_cast<u64>(action.sa_sigaction ? (void*)action.sa_sigaction : (void*)action.sa_handler);
    frame->rsp = rsp;
    
    // Argument passing (x86_64 ABI): rdi, rsi, rdx
    frame->rdi = sig;
    if (action.sa_flags & SA_SIGINFO) {
        frame->rsi = reinterpret_cast<u64>(&uframe->info);
        frame->rdx = reinterpret_cast<u64>(&uframe->uc);
    } else {
        frame->rsi = 0;
        frame->rdx = 0;
    }

    // Set return address to restorer (trampoline)
    // Handler will 'ret' to this
    *reinterpret_cast<u64*>(rsp - 8) = reinterpret_cast<u64>(action.sa_restorer);
    frame->rsp -= 8;
}

void Signal::DoPendingSignals(cpu::InterruptFrame* frame) {
    auto* thread = Scheduler::GetCurrentThread();
    if (!thread) return;

    // Only deliver signals when returning to user-space.
    if ((frame->cs & 3) == 0) return;

    u64 pending = thread->PendingSignals() & ~thread->BlockedSignals();
    if (!pending) return;

    auto* process = thread->GetProcess();
    if (!process) return;

    // Do not deliver signals to a zombie or already-terminated process.
    // This prevents double-delivery when _exit() is called from a signal handler:
    //   1. SIGSEGV fires → handler runs → _exit(0) → process->Exit() → zombie
    //   2. On the next interrupt return, DoPendingSignals is called again.
    //      Without this guard, the pending bit (set by a concurrent fault during
    //      SetupFrame) would trigger a second delivery on the zombie process.
    if (process->IsZombie()) return;
    if (thread->State() == ThreadState::Terminated) return;

    for (int i = 1; i < NSIG; ++i) {
        if (!(pending & (1ULL << (i - 1)))) continue;

        // Clear the pending bit first — prevents re-delivery if we fault below.
        thread->PendingSignals() &= ~(1ULL << (i - 1));

        const auto& action = process->GetSignalAction(i);

        if (action.sa_handler == SIG_IGN) {
            continue;
        }

        if (action.sa_handler == SIG_DFL) {
            // Default action for fatal signals: exit the process.
            // Use Exit() → zombie → waitpid, NOT Destroy() (which is an
            // immediate teardown that leaves the thread running with a freed
            // process pointer and destroyed address space, causing a GPF on
            // the iretq back to userspace).
            if (i == SIGKILL || i == SIGSEGV || i == SIGILL ||
                i == SIGFPE  || i == SIGBUS  || i == SIGABRT) {
                FK_LOG_ERR("Signal: Process {} terminated by signal {}", process->GetPid(), i);

                // Transition process to zombie and wake any waiting parent.
                // The address space is NOT freed here — it stays alive until
                // the thread is fully off the CPU and the Reaper runs.
                process->Exit(-(i));

                // Mark this thread as terminated and hand it to the Reaper.
                // Scheduler::Yield() switches away and never returns here,
                // so we never iretq back into the now-zombie address space.
                thread->SetState(ThreadState::Terminated);
                Reaper::Enqueue(thread);
                Scheduler::Yield(); // Does not return.
                return;
            }
            // All other default actions are ignore.
            continue;
        }

        // User-installed handler — set up the signal frame and return to it.
        SetupFrame(frame, i, action);
        return; // Deliver one signal at a time.
    }
}

extern "C" void SignalCheckPending(ceryx::cpu::InterruptFrame* frame) {
    ceryx::proc::Signal::DoPendingSignals(frame);
}

} // namespace ceryx::proc
