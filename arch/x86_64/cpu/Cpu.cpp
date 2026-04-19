#include <ceryx/cpu/CpuData.hpp>
#include <ceryx/cpu/Syscall.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>
#include <FoundationKitPlatform/Amd64/Cpu.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace ceryx::cpu {

using namespace FoundationKitPlatform::Amd64;

constexpr u32 kMsrGsBase = 0xC0000101;
constexpr u32 kMsrKernelGsBase = 0xC0000102;

extern "C" void arch_cpu_init_per_cpu(CpuData* data) {
    FK_LOG_INFO("ceryx::cpu: Entering arch_cpu_init_per_cpu...");
    FK_BUG_ON(data == nullptr, "arch_cpu_init_per_cpu: data is null");

    // 0. Ensure the self pointer is correct and set GS bases immediately.
    // GS_BASE is for kernel use, KERNEL_GS_BASE will be used by userspace
    // and swapped in/out during transitions.
    data->self = data;
    FK_LOG_INFO("ceryx::cpu: Setting GS bases to {:#x}...", reinterpret_cast<u64>(data));
    ControlRegs::WriteMsr(kMsrGsBase, reinterpret_cast<u64>(data));
    ControlRegs::WriteMsr(kMsrKernelGsBase, 0); // User GS base starts at 0
    FK_LOG_INFO("ceryx::cpu: GS bases set.");
    
    // 1. Enable FPU and SSE
    FK_LOG_INFO("ceryx::cpu: Enabling FPU and SSE...");
    u64 cr0 = ControlRegs::ReadCr0();
    cr0 &= ~(1ULL << 2); // ~EM (Emulation)
    cr0 |= (1ULL << 1);  // MP (Monitor Coprocessor)
    cr0 |= (1ULL << 5);  // NE (Numeric Error)
    ControlRegs::WriteCr0(cr0);
    FK_LOG_INFO("ceryx::cpu: CR0 updated.");

    // Initialize SYSCALL support.
    FK_LOG_INFO("ceryx::cpu: Initializing SYSCALL...");
    Syscall::Initialize();
    FK_LOG_INFO("ceryx::cpu: arch_cpu_init_per_cpu done.");
}

} // namespace ceryx::cpu
