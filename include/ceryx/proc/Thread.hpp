#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitMemory/Management/AddressTypes.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl;

/// @brief x86_64 Register State for a thread.
struct RegisterState {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 interrupt_number, error_code;
    u64 rip, cs, rflags, rsp, ss;
};

class Process;

/// @brief Represents a single thread of execution.
class Thread {
public:
    Thread(Process* process, bool is_kernel) noexcept;
    ~Thread() noexcept;

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

private:
    Process* m_process;
    RegisterState m_regs{};
    uptr m_kernel_stack = 0;
};

} // namespace ceryx::proc
