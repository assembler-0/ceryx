#pragma once

#include <ceryx/proc/Thread.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>

namespace ceryx::proc {

class Scheduler {
public:
    // ── MLFQ configuration ───────────────────────────────────────────────────
    static constexpr int kNumLevels = 3;
    /// Tick quanta per MLFQ level (0 = highest priority, smallest quantum).
    static constexpr int kQuantum[kNumLevels] = {5, 10, 20};
    /// Every N ticks all threads are boosted to level 0 (starvation prevention).
    static constexpr u64 kBoostInterval = 100;

    // ── Lifecycle ────────────────────────────────────────────────────────────

    /// @brief Initialize the global MLFQ state.
    static void Initialize();

    /// @brief Start the scheduler on the current BSP (creates idle + reaper threads).
    static void Start();

    // ── Scheduling entry points ──────────────────────────────────────────────

    /// @brief Called from the timer IRQ: advance clock, consume quantum, reschedule if expired.
    static void Tick() noexcept;

    /// @brief Voluntarily yield the remaining quantum (blocking-safe).
    static void Yield();

    /// @brief Block the current thread on a wait channel and immediately reschedule.
    static void Block(void* channel);

    /// @brief Wake threads sleeping on a channel.
    static void Wake(void* channel, bool wake_all = false);

    // ── Thread management ────────────────────────────────────────────────────

    /// @brief Enqueue a thread at the given MLFQ level (default: 0 = highest).
    static void AddThread(Thread* thread, int level = 0);

    /// @brief Get the thread running on this CPU.
    [[nodiscard]] static Thread* GetCurrentThread();

    // ── Internal (visible for Reaper / SwitchContext callers) ────────────────
    struct MlfqImpl;

private:
    static void ScheduleLocked() noexcept;
    static void PerformSwitch(Thread* prev, Thread* next) noexcept;
    static void BoostAll() noexcept;

    static MlfqImpl* s_impl;
    static FoundationKitCxxStl::Sync::SpinLock s_lock;
    static u64 s_tick_counter;
};

} // namespace ceryx::proc
