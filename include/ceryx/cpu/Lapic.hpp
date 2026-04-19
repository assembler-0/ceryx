#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitPlatform/IrqChip/IrqChip.hpp>
#include <FoundationKitPlatform/Clocksource/ClockSource.hpp>

namespace ceryx::cpu {

using namespace FoundationKitCxxStl;
using namespace FoundationKitPlatform::IrqChip;
using namespace FoundationKitPlatform::Clocksource;

class Lapic {
public:
    static constexpr u32 kRegId            = 0x20;
    static constexpr u32 kRegVersion       = 0x30;
    static constexpr u32 kRegTpr           = 0x80;
    static constexpr u32 kRegEoi           = 0xB0;
    static constexpr u32 kRegLdr           = 0xD0;
    static constexpr u32 kRegDfr           = 0xE0;
    static constexpr u32 kRegSvr           = 0xF0;
    static constexpr u32 kRegEsr           = 0x280;
    static constexpr u32 kRegIcrLow        = 0x300;
    static constexpr u32 kRegIcrHigh       = 0x310;
    static constexpr u32 kRegLvtTimer      = 0x320;
    static constexpr u32 kRegLvtThermal    = 0x330;
    static constexpr u32 kRegLvtPerf       = 0x340;
    static constexpr u32 kRegLvtLint0      = 0x350;
    static constexpr u32 kRegLvtLint1      = 0x360;
    static constexpr u32 kRegLvtError      = 0x370;
    static constexpr u32 kRegTimerInitial  = 0x380;
    static constexpr u32 kRegTimerCurrent  = 0x390;
    static constexpr u32 kRegTimerDivide   = 0x3E0;

    /// @brief Initialize the Local APIC for the current CPU.
    static void Initialize();

    /// @brief Send End-of-Interrupt signal.
    static void SendEoi();

    /// @brief Write a value to a LAPIC register.
    static void Write(u32 reg, u32 val);

    /// @brief Read a value from a LAPIC register.
    static u32 Read(u32 reg);

    /// @brief Get the IrqChip descriptor for the LAPIC.
    static IrqChipDescriptor GetIrqChip();

private:
    static uptr s_base;
};

class ApicTimer {
public:
    /// @brief Initialize and calibrate the LAPIC timer.
    static void Initialize();

    /// @brief Set the timer for a one-shot interrupt.
    static void OneShot(u64 nanoseconds);

    /// @brief Set the timer for a periodic interrupt.
    static void Periodic(u64 nanoseconds);

    /// @brief Stop the timer.
    static void Stop();

    /// @brief Calibrate the LAPIC timer and TSC.
    static void Calibrate();

    static u64 GetTscFrequency() { return s_tsc_freq; }
    static u64 GetLapicFrequency() { return s_lapic_freq; }

private:
    static u64 s_tsc_freq;
    static u64 s_lapic_freq;
};

} // namespace ceryx::cpu
