#include <ceryx/cpu/Lapic.hpp>
#include <ceryx/mm/MemoryManager.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>
#include <arch/x86_64/pio.hpp>
#include <arch/x86_64/boot/requests.hpp>
#include <FoundationKitPlatform/Amd64/Cpu.hpp>

namespace ceryx::cpu {

using namespace FoundationKitPlatform::Amd64;
using namespace ceryx::arch::x86_64::pio;
using namespace FoundationKitMemory;

uptr Lapic::s_base = 0;
u64 ApicTimer::s_tsc_freq = 0;
u64 ApicTimer::s_lapic_freq = 0;

void Lapic::Initialize() {
    FK_LOG_INFO("ceryx::cpu::Lapic: Initializing Local APIC...");

    u64 apic_base_msr = ControlRegs::ReadMsr(0x1B);
    u64 apic_phys = apic_base_msr & 0xFFFFFFFFFFFFF000ull;

    // Use a safe canonical virtual address in the kernel's high-half range.
    s_base = 0xFFFFFFFFFEE00000ull;

    FK_LOG_INFO("ceryx::cpu::Lapic: Mapping LAPIC phys {:#x} to VA {:#x}", apic_phys, s_base);

    auto& ptm = mm::MemoryManager::GetPtm();

    // Map the LAPIC physical page. Strong UC (Cacheable=false).
    ptm.Map(VirtualAddress{s_base}, PhysicalAddress{apic_phys}, kPageSize,
            RegionFlags::Writable | RegionFlags::Readable);

    // Flush TLB for the new mapping.
    ptm.FlushTlb(VirtualAddress{s_base});

    // Enable LAPIC by setting the software enable bit in the SVR
    Write(kRegSvr, Read(kRegSvr) | 0x1FF); // Vector 0xFF for spurious, bit 8 for enable
    
    FK_LOG_INFO("ceryx::cpu::Lapic: Local APIC enabled.");
}

void Lapic::Write(u32 reg, u32 val) {
    *reinterpret_cast<volatile u32*>(s_base + reg) = val;
}

u32 Lapic::Read(u32 reg) {
    return *reinterpret_cast<volatile u32*>(s_base + reg);
}

void Lapic::SendEoi() {
    Write(kRegEoi, 0);
}

IrqChipDescriptor Lapic::GetIrqChip() {
    IrqChipDescriptor d;
    d.name = "Local APIC";
    d.EndOfInterrupt = [](const IrqData&) noexcept {
        SendEoi();
    };
    return d;
}

// ----------------------------------------------------------------------------
// APIC Timer Calibration
// ----------------------------------------------------------------------------

static void PitPrepare(u16 count) {
    outb(0x43, 0x34); // Channel 0, lobyte/hibyte, rate generator
    outb(0x40, count & 0xFF);
    outb(0x40, (count >> 8) & 0xFF);
}

void ApicTimer::Calibrate() {
    FK_LOG_INFO("ceryx::cpu::ApicTimer: Calibrating timers against PIT...");

    // We'll use the PIT to calibrate for 10ms (11932 ticks)
    Lapic::Write(Lapic::kRegTimerDivide, 0x03); // Divide by 16
    PitPrepare(11932);

    u64 tsc_start = Rdtsc();
    Lapic::Write(Lapic::kRegTimerInitial, 0xFFFFFFFF);

    // Wait for the PIT to wrap. PIT counts DOWN.
    // Wrap detected when current count > last count.
    u16 last_count = 0xFFFF;
    while (true) {
        outb(0x43, 0x00); // Latch channel 0
        u16 count = inb(0x40) | (inb(0x40) << 8);
        if (count > last_count) break;
        last_count = count;
    }

    u64 tsc_end = Rdtsc();
    u32 lapic_ticks = 0xFFFFFFFF - Lapic::Read(Lapic::kRegTimerCurrent);

    s_tsc_freq = (tsc_end - tsc_start) * 100; // 10ms * 100 = 1s
    s_lapic_freq = static_cast<u64>(lapic_ticks) * 100;
    
    FK_LOG_INFO("ceryx::cpu::ApicTimer: Calibration done. TSC: {} MHz, LAPIC: {} MHz", 
                s_tsc_freq / 1000000, (s_lapic_freq * 16) / 1000000);
}

void ApicTimer::Initialize() {
    Calibrate();
    
    // Setup Timer interrupt (Vector 32)
    Lapic::Write(Lapic::kRegLvtTimer, 32); 
    Lapic::Write(Lapic::kRegTimerDivide, 0x03); // Divide by 16
}

void ApicTimer::OneShot(u64 nanoseconds) {
    u64 ticks = (nanoseconds * s_lapic_freq) / 1000000000ull;
    if (ticks == 0) ticks = 1;
    Lapic::Write(Lapic::kRegTimerInitial, static_cast<u32>(ticks));
}

void ApicTimer::Periodic(u64 nanoseconds) {
    u64 ticks = (nanoseconds * s_lapic_freq) / 1000000000ull;
    Lapic::Write(Lapic::kRegLvtTimer, 32 | (1 << 17)); // Vector 32, Periodic
    Lapic::Write(Lapic::kRegTimerInitial, static_cast<u32>(ticks));
}

void ApicTimer::Stop() {
    Lapic::Write(Lapic::kRegTimerInitial, 0);
}

} // namespace ceryx::cpu
