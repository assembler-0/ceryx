#pragma once

#include <ceryx/cpu/InterruptFrame.hpp>

namespace ceryx::cpu {

using namespace FoundationKitCxxStl;

class Syscall {
public:
    /// @brief Initialize syscall support for the current CPU.
    static void Initialize();

    /// @brief Main dispatcher for syscalls.
    /// @param regs The register state at the time of the syscall.
    /// @return The return value of the syscall.
    static u64 Dispatch(InterruptFrame* regs);
};

} // namespace ceryx::cpu
