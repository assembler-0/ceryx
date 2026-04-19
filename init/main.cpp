#include <FoundationKitCxxStl/Base/Version.hpp>
#include <lib/linearfb.hpp>
#include <ceryx/mm/MemoryManager.hpp>
#include <ceryx/cpu/CpuData.hpp>
#include <ceryx/cpu/Gdt.hpp>
#include <ceryx/cpu/Idt.hpp>
#include <arch/x86_64/boot/requests.hpp>

using namespace FoundationKitCxxStl;

extern "C" void arch_cpu_init_per_cpu(ceryx::cpu::CpuData* data);

extern "C" void start_kernel() {
    linearfb_console_init();
    FK_LOG_INFO("ceryx (LaDK) - Literally a Demonstration Kernel - FoundationKit (R) reference kernel implementation");
    PrintFoundationKitInfo();

    // Initialize BSP Per-CPU data
    static ceryx::cpu::CpuData bsp_cpu_data;
    static u8 bsp_kernel_stack[16384]; // 16 KiB
    
    bsp_cpu_data.cpu_id = 0;
    bsp_cpu_data.lapic_id = 0;
    bsp_cpu_data.kernel_stack = reinterpret_cast<uptr>(bsp_kernel_stack + sizeof(bsp_kernel_stack));
    
    arch_cpu_init_per_cpu(&bsp_cpu_data);
    
    // Initialize GDT and IDT
    ceryx::cpu::Gdt::Initialize();
    ceryx::cpu::Idt::Initialize();

    // Initialize Memory Management
    FK_BUG_ON(!(get_memmap_request()->response || get_hhdm_request()->response), "ceryx::start_kernel: no memory map or HHDM found");
    ceryx::mm::MemoryManager::Initialize(get_memmap_request()->response, get_hhdm_request()->response);

    // Test Global Allocator
    FK_LOG_INFO("ceryx::start_kernel: testing global allocator...");
    int* test_ptr = new int(42);
    FK_BUG_ON(*test_ptr != 42, "ceryx::start_kernel: global allocator test failed (value mismatch)");
    FK_LOG_INFO("ceryx::start_kernel: allocated int at {} with value {}", static_cast<void*>(test_ptr), *test_ptr);
    delete test_ptr;
    FK_LOG_INFO("ceryx::start_kernel: global allocator test passed.");

    // Test Per-CPU System
    FK_LOG_INFO("ceryx::start_kernel: testing per-cpu system...");
    u32 current_cpu = FoundationKitOsl::OslGetCurrentCpuId();
    FK_BUG_ON(current_cpu != 0, "ceryx::start_kernel: per-cpu test failed (cpu_id mismatch)");
    FK_LOG_INFO("ceryx::start_kernel: current cpu id: {}", current_cpu);
    
    void* per_cpu_base = FoundationKitOsl::OslGetPerCpuBase();
    FK_BUG_ON(per_cpu_base != &bsp_cpu_data, "ceryx::start_kernel: per-cpu test failed (base address mismatch)");
    FK_LOG_INFO("ceryx::start_kernel: per-cpu base: {}", per_cpu_base);
    FK_LOG_INFO("ceryx::start_kernel: per-cpu system test passed.");

    FK_LOG_INFO("ceryx::start_kernel: init done.");

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}