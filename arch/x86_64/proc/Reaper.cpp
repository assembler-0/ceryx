#include <ceryx/proc/Reaper.hpp>
#include <ceryx/proc/Scheduler.hpp>
#include <ceryx/proc/Process.hpp>
#include <FoundationKitCxxStl/Sync/InterruptSafe.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>

namespace ceryx::proc {

Reaper::State Reaper::s_state;

void Reaper::Initialize() noexcept {
    s_state.reaper_thread = new Thread(nullptr, true);
    s_state.reaper_thread->InitializeStack(reinterpret_cast<uptr>(ThreadFunc), 0);
    s_state.reaper_thread->SetName("reaper");

    Scheduler::AddThread(s_state.reaper_thread, Scheduler::kNumLevels - 1); // Lowest MLFQ level.
}

void Reaper::Enqueue(Thread* thread) noexcept {
    FK_BUG_ON(thread == s_state.reaper_thread, "Reaper: reaper cannot reap itself!");
    FK_BUG_ON(thread->State() != ThreadState::Terminated,
              "Reaper: thread must be Terminated before enqueueing");

    InterruptSafeSpinLock guard(s_state.lock);
    s_state.dead_list.PushBack(&thread->run_node);
}

void Reaper::ThreadFunc(uptr) noexcept {
    for (;;) {
        IntrusiveDoublyLinkedList local_dead;

        {
            InterruptSafeSpinLock guard(s_state.lock);
            while (!s_state.dead_list.Empty()) {
                local_dead.PushBack(s_state.dead_list.PopFront());
            }
        }

        while (!local_dead.Empty()) {
            auto* node   = local_dead.PopFront();
            auto* thread = ContainerOf<Thread, &Thread::run_node>(node);

            FK_LOG_INFO("Reaper: Reclaiming thread {} (Process {})",
                thread->GetId(),
                thread->GetProcess() ? thread->GetProcess()->GetPid() : 0ULL);
            delete thread;
        }

        Scheduler::Yield();
    }
}

} // namespace ceryx::proc
