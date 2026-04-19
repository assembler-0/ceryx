#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

namespace ceryx::cpu {

using namespace FoundationKitCxxStl;

/// @brief Structure containing per-CPU data for the Ceryx kernel.
/// @note  The first member MUST be a pointer to the structure itself to
///        allow for efficient access via the GS segment register.
struct CpuData {
    /// @brief Self-pointer for GS:0 access.
    CpuData* self;

    /// @brief Logical CPU index.
    u32 cpu_id;

    /// @brief Current lapic id (if applicable).
    u32 lapic_id;

    /// @brief Kernel stack pointer for this CPU (e.g. for syscalls).
    uptr kernel_stack;

    /// @brief Temporary storage for user RSP during syscalls.
    uptr user_rsp;

    /// @brief Per-CPU allocator instance (if we decide to have one).
    void* per_cpu_allocator;

    /// @brief Reserved for future use.
    u64 reserved[8];
};

} // namespace ceryx::cpu
