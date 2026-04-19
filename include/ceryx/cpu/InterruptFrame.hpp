#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

namespace ceryx::cpu {

using namespace FoundationKitCxxStl;

/// @brief Canonical x86_64 Interrupt/Syscall frame.
/// @note  This MUST stay in sync with assembly entry points in
///        Interrupts.asm and Syscall.asm.
struct InterruptFrame {
    // General Purpose Registers (pushed by common stub)
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;

    // Metadata (pushed by ISR stub or SyscallEntry)
    u64 interrupt_number; 
    u64 error_code;

    // IRET context (pushed by hardware or SyscallEntry)
    u64 rip, cs, rflags, rsp, ss;
} __attribute__((packed));

} // namespace ceryx::cpu
