#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitMemory/Management/AddressTypes.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <ceryx/cpu/InterruptFrame.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Structure;


enum class ThreadState {
    Ready,
    Running,
    Blocked,
    Terminated,
    Zombie,   ///< Thread finished but Process not yet waited on.
};

class Process;

/// @brief Represents a single thread of execution.
class Thread {
public:
    Thread(Process* process, bool is_kernel) noexcept;
    ~Thread() noexcept;

    /// @brief Initialize the thread's stack for the first run.
    /// @param entry_point The function to execute.
    /// @param arg The argument to pass to the entry point.
    void InitializeStack(uptr entry_point, uptr arg) noexcept;

    /// @brief Initialize the thread's stack for jumping into Ring 3.
    /// @param user_rip The entry point in userspace.
    /// @param user_rsp The stack pointer in userspace.
    void InitializeUserStack(uptr user_rip, uptr user_rsp) noexcept;

    /// @brief Build a fork child's kernel stack: restores parent's InterruptFrame
    ///        via ForkReturn trampoline, with rax=0 (fork returns 0 in child).
    void InitializeForkStack(const cpu::InterruptFrame& parent_frame) noexcept;

    /// @brief Set the instruction pointer for the thread.
    void SetEntry(uptr rip) noexcept { m_regs.rip = rip; }

    /// @brief Set the stack pointer for the thread.
    void SetStack(uptr rsp) noexcept { m_regs.rsp = rsp; }

    /// @brief Get the thread's register state.
    [[nodiscard]] cpu::InterruptFrame& Registers() noexcept { return m_regs; }

    /// @brief Get the process this thread belongs to.
    [[nodiscard]] Process* GetProcess() const noexcept { return m_process; }

    /// @brief Get the kernel stack for this thread.
    [[nodiscard]] uptr GetKernelStack() const noexcept { return m_kernel_stack; }

    /// @brief Get/Set thread state.
    [[nodiscard]] ThreadState State() const noexcept { return m_state; }
    void SetState(ThreadState state) noexcept { m_state = state; }

    /// @brief Get the unique thread identifier.
    [[nodiscard]] u64 GetId() const noexcept { return m_tid; }

    /// @brief Linkage for the scheduler's runqueue.
    IntrusiveDoublyLinkedListNode run_node;

    /// @brief Linkage for wait channels.
    IntrusiveDoublyLinkedListNode wait_node;

    /// @brief Channel (object) the thread is waiting on.
    void* wait_channel = nullptr;

    /// @brief Set the thread's debug name.
    void SetName(const char* name) noexcept;

    [[nodiscard]] u64& PendingSignals() noexcept { return m_pending_signals; }
    [[nodiscard]] u64 PendingSignals() const noexcept { return m_pending_signals; }

    [[nodiscard]] u64& BlockedSignals() noexcept { return m_blocked_signals; }
    [[nodiscard]] u64 BlockedSignals() const noexcept { return m_blocked_signals; }

    // MLFQ scheduler fields.
    [[nodiscard]] int  MlfqLevel() const noexcept { return m_mlfq_level; }
    void SetMlfqLevel(int l) noexcept { m_mlfq_level = l; }

    [[nodiscard]] int  TicksRemaining() const noexcept { return m_ticks_remaining; }
    void SetTicksRemaining(int t) noexcept { m_ticks_remaining = t; }
    bool ConsumeOneTick() noexcept { return --m_ticks_remaining <= 0; } ///< Returns true when quantum expires.

    [[nodiscard]] u64 TotalTicks() const noexcept { return m_total_ticks; }
    void IncrementTotalTicks() noexcept { ++m_total_ticks; }

private:
    Process* m_process;
    u64 m_tid;
    char m_name[32]{};
    cpu::InterruptFrame m_regs{};
    uptr m_kernel_stack = 0;
    ThreadState m_state = ThreadState::Ready;
    u64 m_priority = 0;

    // MLFQ state.
    int m_mlfq_level      = 0; ///< Current MLFQ priority level (0 = highest).
    int m_ticks_remaining = 5; ///< Ticks left in current quantum.
    u64 m_total_ticks     = 0; ///< Total ticks consumed (for aging/boost decisions).

    static u64 s_next_tid;

    u64 m_pending_signals = 0;
    u64 m_blocked_signals = 0;
};

} // namespace ceryx::proc
