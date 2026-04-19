#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitMemory/Management/AddressTypes.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Structure;

/// @brief x86_64 Register State for a thread.
struct RegisterState {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 interrupt_number, error_code;
    u64 rip, cs, rflags, rsp, ss;
} __attribute__((packed));

enum class ThreadState {
    Ready,
    Running,
    Blocked,
    Terminated
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

    /// @brief Set the instruction pointer for the thread.
    void SetEntry(uptr rip) noexcept { m_regs.rip = rip; }

    /// @brief Set the stack pointer for the thread.
    void SetStack(uptr rsp) noexcept { m_regs.rsp = rsp; }

    /// @brief Get the thread's register state.
    [[nodiscard]] RegisterState& Registers() noexcept { return m_regs; }

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

private:
    Process* m_process;
    u64 m_tid;
    RegisterState m_regs{};
    uptr m_kernel_stack = 0;
    ThreadState m_state = ThreadState::Ready;
    u64 m_priority = 0;

    static u64 s_next_tid;
};

} // namespace ceryx::proc
