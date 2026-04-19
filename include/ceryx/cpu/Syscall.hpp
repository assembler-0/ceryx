#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

namespace ceryx::cpu {

using namespace FoundationKitCxxStl;

/// @brief Registers to be saved during a syscall.
struct SyscallRegisters {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 rip, cs, rflags, rsp, ss;
} __attribute__((packed));

class Syscall {
public:
    /// @brief Initialize syscall support for the current CPU.
    static void Initialize();

    /// @brief Main dispatcher for syscalls.
    /// @param regs The register state at the time of the syscall.
    /// @return The return value of the syscall.
    static u64 Dispatch(SyscallRegisters* regs);
};

} // namespace ceryx::cpu
