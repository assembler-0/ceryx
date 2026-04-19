#include <ceryx/proc/Signal.hpp>
#include <ceryx/proc/Thread.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/proc/Scheduler.hpp>
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

    // Ensure we are returning to user-space
    if ((frame->cs & 3) == 0) return;

    u64 pending = thread->PendingSignals() & ~thread->BlockedSignals();
    if (!pending) return;

    auto* process = thread->GetProcess();
    if (!process) return;

    for (int i = 1; i < NSIG; ++i) {
        if (pending & (1ULL << (i - 1))) {
            // Peek at the signal
            const auto& action = process->GetSignalAction(i);
            
            if (action.sa_handler == SIG_IGN) {
                thread->PendingSignals() &= ~(1ULL << (i - 1));
                continue;
            }
            
            if (action.sa_handler == SIG_DFL) {
                // Default actions
                if (i == SIGKILL || i == SIGSEGV || i == SIGILL || i == SIGFPE) {
                    FK_LOG_ERR("Signal: Process {} terminated by signal {}", process->GetPid(), i);
                    process->Destroy();
                    return; 
                }
                // Many defaults are IGN
                thread->PendingSignals() &= ~(1ULL << (i - 1));
                continue;
            }

            // Clear pending bit BEFORE setup to prevent infinite recursion if SetupFrame faults
            thread->PendingSignals() &= ~(1ULL << (i - 1));

            // Handler case
            SetupFrame(frame, i, action);
            
            return; // Deliver one signal at a time
        }
    }
}

extern "C" void SignalCheckPending(ceryx::cpu::InterruptFrame* frame) {
    ceryx::proc::Signal::DoPendingSignals(frame);
}

} // namespace ceryx::proc
