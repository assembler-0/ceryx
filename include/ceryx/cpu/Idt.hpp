#pragma once

#include <ceryx/cpu/InterruptFrame.hpp>

namespace ceryx::cpu {

using namespace FoundationKitCxxStl;

extern "C" void InterruptDispatch(InterruptFrame* frame);

/// @brief x86_64 IDT Gate types
enum class IdtGateType : u8 {
    InterruptGate = 0xE,
    TrapGate      = 0xF,
};

/// @brief IDT Entry (Gate Descriptor)
struct IdtGate {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attributes;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;

    void SetOffset(u64 offset) {
        offset_low = offset & 0xFFFF;
        offset_mid = (offset >> 16) & 0xFFFF;
        offset_high = (offset >> 32) & 0xFFFFFFFF;
    }

    void SetAttributes(u8 dpl, IdtGateType type) {
        // Present (1), DPL (2 bits), 0, Type (4 bits)
        type_attributes = 0x80 | ((dpl & 0x3) << 5) | (static_cast<u8>(type) & 0xF);
    }
} __attribute__((packed));

/// @brief IDT Pointer for lidt
struct IdtPointer {
    u16 limit;
    u64 base;
} __attribute__((packed));

using InterruptHandler = void (*)(InterruptFrame* frame);

class Idt {
public:
    static constexpr usize kIdtSize = 256;

    /// @brief Initialize the Interrupt Descriptor Table.
    static void Initialize();

    /// @brief Register a high-level handler for an interrupt.
    static void RegisterHandler(u8 vector, InterruptHandler handler);

private:
    static IdtGate s_idt[kIdtSize];
    static InterruptHandler s_handlers[kIdtSize];

    friend void InterruptDispatch(InterruptFrame* frame);
};

} // namespace ceryx::cpu
