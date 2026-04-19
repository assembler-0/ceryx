#include <ceryx/proc/Scheduler.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/cpu/Gdt.hpp>
#include <ceryx/cpu/Lapic.hpp>
#include <ceryx/cpu/CpuData.hpp>
#include <ceryx/proc/Reaper.hpp>
#include <ceryx/mm/MemoryManager.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Sync/InterruptSafe.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl::Sync;
using namespace FoundationKitCxxStl::Structure;
using namespace FoundationKitPlatform::Amd64;

extern "C" void SwitchContext(u64* prev_rsp, u64 next_rsp);

// ── Static members ────────────────────────────────────────────────────────────

Scheduler::MlfqImpl* Scheduler::s_impl          = nullptr;
SpinLock             Scheduler::s_lock;
u64                  Scheduler::s_tick_counter   = 0;

// ── Wait queue (blocked threads) ─────────────────────────────────────────────

struct WaitEntry {
    IntrusiveDoublyLinkedList list;
};
static WaitEntry g_wait_queue;

// ── MLFQ implementation ───────────────────────────────────────────────────────

struct Scheduler::MlfqImpl {
    IntrusiveDoublyLinkedList queues[kNumLevels];

    /// Push a thread into its current MLFQ level queue.
    void Push(Thread* t) noexcept {
        int lvl = t->MlfqLevel();
        if (lvl < 0)          lvl = 0;
        if (lvl >= kNumLevels) lvl = kNumLevels - 1;
        queues[lvl].PushBack(&t->run_node);
    }

    /// Pop the highest-priority ready thread (scans level 0 first).
    Thread* PopBest() noexcept {
        for (int i = 0; i < kNumLevels; ++i) {
            if (!queues[i].Empty()) {
                auto* node = queues[i].PopFront();
                return ContainerOf<Thread, &Thread::run_node>(node);
            }
        }
        return nullptr;
    }

    bool Empty() const noexcept {
        for (int i = 0; i < kNumLevels; ++i)
            if (!queues[i].Empty()) return false;
        return true;
    }

    /// Move all threads from levels 1..N-1 to level 0 (priority boost).
    void Boost() noexcept {
        for (int i = 1; i < kNumLevels; ++i) {
            while (!queues[i].Empty()) {
                auto* node = queues[i].PopFront();
                auto* t    = ContainerOf<Thread, &Thread::run_node>(node);
                t->SetMlfqLevel(0);
                t->SetTicksRemaining(kQuantum[0]);
                queues[0].PushBack(&t->run_node);
            }
        }
    }
};

static Scheduler::MlfqImpl g_mlfq_impl;

// ── Idle thread ───────────────────────────────────────────────────────────────

static void idle_func(uptr) {
    for (;;) { __asm__ volatile("hlt"); }
}

// ── Public API ────────────────────────────────────────────────────────────────

void Scheduler::Initialize() {
    s_impl = &g_mlfq_impl;
}

void Scheduler::Start() {
    if (!s_impl) Initialize();

    auto* cpu_data = static_cast<cpu::CpuData*>(FoundationKitOsl::OslGetPerCpuBase());

    Thread* idle = new Thread(nullptr, true);
    idle->InitializeStack(reinterpret_cast<uptr>(idle_func), 0);
    idle->SetState(ThreadState::Running);
    idle->SetMlfqLevel(kNumLevels - 1); // idle lives in the lowest-priority bucket

    cpu_data->idle_thread    = idle;
    cpu_data->current_thread = idle;

    u64 stack_top = idle->GetKernelStack() + 32768;
    cpu::Gdt::SetStack(cpu::PrivilegeLevel::Ring0, stack_top);
    cpu_data->kernel_stack = stack_top;

    Reaper::Initialize();

    FK_LOG_INFO("ceryx::proc::Scheduler: Started on CPU {}", cpu_data->cpu_id);
}

void Scheduler::AddThread(Thread* thread, int level) {
    FK_BUG_ON(level < 0 || level >= kNumLevels,
        "Scheduler::AddThread: invalid MLFQ level {}", level);

    thread->SetMlfqLevel(level);
    thread->SetTicksRemaining(kQuantum[level]);
    thread->SetState(ThreadState::Ready);

    InterruptSafeSpinLock guard(s_lock);
    s_impl->queues[level].PushBack(&thread->run_node);
}

// ── Tick — called from timer IRQ ─────────────────────────────────────────────

void Scheduler::Tick() noexcept {
    InterruptSafeSpinLock guard(s_lock);

    auto* cpu_data = static_cast<cpu::CpuData*>(FoundationKitOsl::OslGetPerCpuBase());
    if (!cpu_data) return;

    ++s_tick_counter;

    // Priority boost every kBoostInterval ticks (starvation prevention).
    if (s_tick_counter % kBoostInterval == 0) {
        BoostAll();
    }

    auto* current = static_cast<Thread*>(cpu_data->current_thread);

    // Don't charge the idle thread.
    if (current && current != cpu_data->idle_thread) {
        current->IncrementTotalTicks();

        if (current->ConsumeOneTick()) {
            // Quantum expired → demote to next level and reschedule.
            int new_level = current->MlfqLevel() + 1;
            if (new_level >= kNumLevels) new_level = kNumLevels - 1;

            current->SetMlfqLevel(new_level);
            current->SetTicksRemaining(kQuantum[new_level]);
            current->SetState(ThreadState::Ready);
            s_impl->queues[new_level].PushBack(&current->run_node);

            ScheduleLocked();
        }
        // If quantum not expired, keep running the current thread.
    } else {
        // Idle thread: try to pick a real thread if one is ready.
        if (!s_impl->Empty()) {
            ScheduleLocked();
        }
    }
}

// ── Yield ─────────────────────────────────────────────────────────────────────

void Scheduler::Yield() {
    InterruptSafeSpinLock guard(s_lock);
    auto* cpu_data = static_cast<cpu::CpuData*>(FoundationKitOsl::OslGetPerCpuBase());
    auto* current  = static_cast<Thread*>(cpu_data->current_thread);

    // Re-enqueue at the same level (voluntary yield doesn't demote).
    if (current && current != cpu_data->idle_thread &&
        current->State() == ThreadState::Running) {
        current->SetState(ThreadState::Ready);
        // Reset quantum on voluntary yield (I/O-bound reward).
        current->SetTicksRemaining(kQuantum[current->MlfqLevel()]);
        s_impl->queues[current->MlfqLevel()].PushBack(&current->run_node);
    }

    ScheduleLocked();
}

// ── Block / Wake ──────────────────────────────────────────────────────────────

void Scheduler::Block(void* channel) {
    InterruptSafeSpinLock guard(s_lock);
    auto* thread = GetCurrentThread();
    if (!thread) return;

    thread->SetState(ThreadState::Blocked);
    thread->wait_channel = channel;
    g_wait_queue.list.PushBack(&thread->wait_node);

    ScheduleLocked();
}

void Scheduler::Wake(void* channel, bool wake_all) {
    InterruptSafeSpinLock guard(s_lock);

    auto* node = g_wait_queue.list.Begin();
    while (node != g_wait_queue.list.End()) {
        auto* next   = node->next;
        auto* thread = ContainerOf<Thread, &Thread::wait_node>(node);

        if (thread->wait_channel == channel) {
            g_wait_queue.list.Remove(node);
            thread->wait_channel = nullptr;
            thread->SetState(ThreadState::Ready);
            // Wake at the same MLFQ level the thread was blocked at (I/O reward).
            thread->SetTicksRemaining(kQuantum[thread->MlfqLevel()]);
            s_impl->queues[thread->MlfqLevel()].PushBack(&thread->run_node);

            if (!wake_all) break;
        }
        node = next;
    }
}

// ── GetCurrentThread ──────────────────────────────────────────────────────────

Thread* Scheduler::GetCurrentThread() {
    auto* cpu_data = static_cast<cpu::CpuData*>(FoundationKitOsl::OslGetPerCpuBase());
    return static_cast<Thread*>(cpu_data->current_thread);
}

// ── Internal helpers ──────────────────────────────────────────────────────────

void Scheduler::BoostAll() noexcept {
    s_impl->Boost();
}

void Scheduler::ScheduleLocked() noexcept {
    auto* cpu_data = static_cast<cpu::CpuData*>(FoundationKitOsl::OslGetPerCpuBase());
    if (!cpu_data) return;

    auto* prev = static_cast<Thread*>(cpu_data->current_thread);

    // Pick the best ready thread; fall back to idle.
    Thread* next = s_impl->PopBest();
    if (!next) {
        next = static_cast<Thread*>(cpu_data->idle_thread);
    }

    if (!next || next == prev) return;

    PerformSwitch(prev, next);
}

void Scheduler::PerformSwitch(Thread* prev, Thread* next) noexcept {
    auto* cpu_data = static_cast<cpu::CpuData*>(FoundationKitOsl::OslGetPerCpuBase());

    cpu_data->current_thread = next;
    next->SetState(ThreadState::Running);

    // Update TSS / CpuData kernel stack for Ring 0 entry on the new thread.
    u64 stack_top = next->GetKernelStack() + 32768;
    cpu::Gdt::SetStack(cpu::PrivilegeLevel::Ring0, stack_top);
    cpu_data->kernel_stack = stack_top;

    // Switch address space if needed.
    FoundationKitMemory::PhysicalAddress root_pa;
    if (next->GetProcess()) {
        root_pa = next->GetProcess()->GetAddressSpace().GetRootPa();
    } else {
        root_pa = mm::MemoryManager::GetPtm().RootPhysicalAddress();
    }
    if (ControlRegs::ReadCr3() != root_pa.value) {
        ControlRegs::WriteCr3(root_pa.value);
    }

    if (prev) {
        SwitchContext(&prev->Registers().rsp, next->Registers().rsp);
    } else {
        static u64 dummy_rsp;
        SwitchContext(&dummy_rsp, next->Registers().rsp);
    }
}

} // namespace ceryx::proc
