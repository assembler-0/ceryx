#include <FoundationKitCxxStl/Base/Version.hpp>
#include <lib/linearfb.hpp>
#include <ceryx/mm/MemoryManager.hpp>
#include <ceryx/cpu/CpuData.hpp>
#include <ceryx/cpu/Gdt.hpp>
#include <ceryx/cpu/Idt.hpp>
#include <ceryx/cpu/Lapic.hpp>
#include <ceryx/proc/Scheduler.hpp>
#include <ceryx/proc/Process.hpp>
#include <arch/x86_64/boot/requests.hpp>
#include <FoundationKitPlatform/Amd64/Clocksource.hpp>
#include <FoundationKitPlatform/Clocksource/TimeKeeper.hpp>
#include <FoundationKitPlatform/Amd64/Cpu.hpp>

using namespace FoundationKitPlatform::Clocksource;
using namespace FoundationKitCxxStl;
using namespace ceryx::proc;
using namespace FoundationKitPlatform::Amd64;

extern "C" void arch_cpu_init_per_cpu(ceryx::cpu::CpuData* data);

void test_thread_func(uptr) {
    FK_LOG_INFO("ceryx::test_thread_func: Hello from thread!");
    for (;;) {
        FK_LOG_INFO("ceryx::test_thread_func: thread heartbeat...");
        FoundationKitOsl::OslMicroDelay(2000000); // 2s
    }
}

// Small userspace test program
static const u8 kUserPayload[] = {
    0x48, 0xC7, 0xC0, 0x00, 0x00, 0x00, 0x00, // .loop: mov rax, 0
    0x0F, 0x05,                               // syscall
    0xEB, 0xF5                                // jmp .loop (offset -11)
};

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
    
    // 1. Setup GDT and IDT first. This is the foundation for all interrupt handling.
    ceryx::cpu::Gdt::Initialize();
    ceryx::cpu::Idt::Initialize();

    // 2. Initialize CPU features AFTER IDT is set up.
    // If a feature enablement fails, the IDT will catch the exception and log it.
    arch_cpu_init_per_cpu(&bsp_cpu_data);

    // 3. Initialize Memory Management
    FK_BUG_ON(!(get_memmap_request()->response || get_hhdm_request()->response), "ceryx::start_kernel: no memory map or HHDM found");
    ceryx::mm::MemoryManager::Initialize(get_memmap_request()->response, get_hhdm_request()->response);

    // 4. Initialize APIC and Timer
    ceryx::cpu::Lapic::Initialize();
    ceryx::cpu::ApicTimer::Initialize();

    // 5. Register TSC with TimeKeeper
    u64 tsc_freq = ceryx::cpu::ApicTimer::GetTscFrequency();
    if (HasFeature(CpuFeature::Rdtscp)) {
        auto tsc_source = MakeTscClockSource(tsc_freq);
        TimeKeeper::Register(tsc_source);
        FK_LOG_INFO("ceryx::start_kernel: system time initialized using RDTSCP.");
    } else {
        ClockSourceDescriptor d;
        d.name = "tsc-fallback";
        d.Read = []() noexcept -> u64 { return RdtscFenced(); };
        d.mult = CalibrateMult(tsc_freq);
        d.shift = 32;
        d.mask = ~u64{0};
        d.rating = ClockSourceRating::Excellent;
        d.is_smp_safe = true;
        TimeKeeper::Register(d);
        FK_LOG_INFO("ceryx::start_kernel: system time initialized using RDTSC fallback.");
    }

    // 6. Initialize Scheduler
    Scheduler::Initialize();
    Scheduler::Start();

    // Create a kernel thread
    auto* kthread = new Thread(nullptr, true);
    kthread->InitializeStack(reinterpret_cast<uptr>(test_thread_func), 0);
    Scheduler::AddThread(kthread);

    // Create a userspace process
    FK_LOG_INFO("ceryx::start_kernel: spawning userspace process...");
    auto proc_res = Process::Create();
    FK_BUG_ON(!proc_res.HasValue(), "ceryx::start_kernel: failed to create user process");
    Process* user_proc = proc_res.Value();

    // Allocate and copy user code
    auto& pfa = ceryx::mm::MemoryManager::GetPfa();
    auto code_res = pfa.AllocatePages(1, FoundationKitMemory::RegionType::Generic);
    FK_BUG_ON(!code_res.HasValue(), "ceryx::start_kernel: failed to allocate user code page");
    
    PhysicalAddress code_phys = PfnToPhysical(code_res.Value());
    uptr hhdm_offset = get_hhdm_request()->response->offset;
    void* code_va_ptr = reinterpret_cast<void*>(code_phys.value + hhdm_offset);
    FoundationKitMemory::MemoryCopy(code_va_ptr, kUserPayload, sizeof(kUserPayload));

    // Map the code into the process address space
    VirtualAddress user_code_va{0x400000};
    user_proc->GetAddressSpace().MapUserPage(user_code_va, code_phys, FoundationKitMemory::RegionFlags::Readable | FoundationKitMemory::RegionFlags::Executable);

    // Allocate and map userspace stack
    auto stack_res = pfa.AllocatePages(1, FoundationKitMemory::RegionType::Generic);
    PhysicalAddress stack_phys = PfnToPhysical(stack_res.Value());
    VirtualAddress user_stack_va{0x7FFFFFFF000ull};
    user_proc->GetAddressSpace().MapUserPage(user_stack_va, stack_phys, FoundationKitMemory::RegionFlags::Readable | FoundationKitMemory::RegionFlags::Writable);

    // Create a thread for the user process
    auto* uthread = new Thread(user_proc, false);
    uthread->InitializeUserStack(user_code_va.value, user_stack_va.value + 4096);
    Scheduler::AddThread(uthread);

    FK_LOG_INFO("ceryx::start_kernel: userspace process spawned at {:#x}", user_code_va.value);

    // Start 100Hz heartbeat
    ceryx::cpu::ApicTimer::Periodic(10000000); // 10ms

    FK_LOG_INFO("ceryx::start_kernel: init done.");
    Sti();

    for (;;) {
        Hlt();
    }
}