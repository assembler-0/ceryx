#pragma once

#include <ceryx/proc/Thread.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>

namespace ceryx::proc {

class Scheduler {
public:
    /// @brief Initialize the scheduler.
    static void Initialize();

    /// @brief Start the scheduler (called by the BSP).
    static void Start();

    /// @brief Pick the next thread to run and switch to it.
    static void Schedule();

    /// @brief Yield the current thread's remaining quantum.
    static void Yield();

    /// @brief Add a thread to the runqueue.
    static void AddThread(Thread* thread);

    /// @brief Get the currently running thread on this CPU.
    static Thread* GetCurrentThread();

    // We use a raw pointer to a struct that holds the actual list in the .cpp file
    // to avoid the complex template parsing issue in the header.
    struct RunQueueImpl;

private:
    static RunQueueImpl* s_impl;
    static FoundationKitCxxStl::Sync::SpinLock s_lock;
};

} // namespace ceryx::proc
