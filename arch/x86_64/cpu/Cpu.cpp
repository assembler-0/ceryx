#include <ceryx/cpu/CpuData.hpp>
#include <ceryx/cpu/Syscall.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace ceryx::cpu {

using namespace FoundationKitPlatform::Amd64;

constexpr u32 kMsrGsBase = 0xC0000101;
constexpr u32 kMsrKernelGsBase = 0xC0000102;

extern "C" void arch_cpu_init_per_cpu(CpuData* data) {
    FK_BUG_ON(data == nullptr, "arch_cpu_init_per_cpu: data is null");
    
    // Ensure the self pointer is correct.
    data->self = data;
    
    // Set GS_BASE to point to the CpuData structure.
    ControlRegs::WriteMsr(kMsrGsBase, reinterpret_cast<u64>(data));

    // Initialize SYSCALL support.
    Syscall::Initialize();
}

} // namespace ceryx::cpu
