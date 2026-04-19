#pragma once

#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <ceryx/proc/Thread.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Sync;
using namespace FoundationKitCxxStl::Structure;

/// @brief Asynchronously reclaims resources from terminated threads and processes.
class Reaper {
public:
    /// @brief Initialize the reaper subsystem and start the kernel reaper thread.
    static void Initialize() noexcept;

    /// @brief Add a thread to the dead list for asynchronous reclamation.
    /// @param thread The thread to reclaim. Must be in Terminated state.
    static void Enqueue(Thread* thread) noexcept;

private:
    /// @brief Main loop for the kernel reaper thread.
    static void ThreadFunc(uptr arg) noexcept;

    struct State {
        IntrusiveDoublyLinkedList dead_list;
        SpinLock lock;
        Thread* reaper_thread = nullptr;
    };

    static State s_state;
};

} // namespace ceryx::proc
