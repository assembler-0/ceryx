#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitPlatform/Amd64/Privilege.hpp>

namespace ceryx::cpu {

using namespace FoundationKitCxxStl;
using namespace FoundationKitPlatform::Amd64;

/// @brief x86_64 Task State Segment (TSS)
struct Tss {
    u32 reserved0;
    u64 rsp[3];
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iopb_offset;
} __attribute__((packed));

/// @brief GDT Entry (Descriptor)
struct GdtDescriptor {
    u16 limit_low;
    u16 base_low;
    u8 base_mid;
    u8 access;
    u8 granularity;
    u8 base_high;
} __attribute__((packed));

/// @brief GDT Pointer for lgdt
struct GdtPointer {
    u16 limit;
    u64 base;
} __attribute__((packed));

class Gdt {
public:
    static constexpr u16 kNullSelector       = 0x00;
    static constexpr u16 kKernelCodeSelector = 0x08;
    static constexpr u16 kKernelDataSelector = 0x10;
    static constexpr u16 kUserDataSelector   = 0x18 | 3;
    static constexpr u16 kUserCodeSelector   = 0x20 | 3;
    static constexpr u16 kTssSelector         = 0x28;

    /// @brief Initialize the Global Descriptor Table for the current CPU.
    static void Initialize();

    /// @brief Sets the stack for the given privilege level.
    static void SetStack(PrivilegeLevel level, u64 stack);

    struct GdtTable {
        GdtDescriptor entries[5];
        struct {
            GdtDescriptor low;
            u32 base_upper;
            u32 reserved;
        } tss;
    } __attribute__((packed));

private:

    static void SetupDescriptor(GdtDescriptor& desc, u32 base, u32 limit, u8 access, u8 gran);
    static void SetupTssDescriptor(GdtTable& table, u64 base, u32 limit);
};

} // namespace ceryx::cpu
