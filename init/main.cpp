#include <FoundationKitCxxStl/Base/Version.hpp>
#include <lib/linearfb.hpp>
#include <ceryx/mm/MemoryManager.hpp>
#include <ceryx/cpu/CpuData.hpp>
#include <ceryx/cpu/Gdt.hpp>
#include <ceryx/cpu/Idt.hpp>
#include <ceryx/cpu/Lapic.hpp>
#include <ceryx/proc/Scheduler.hpp>
#include <arch/x86_64/boot/requests.hpp>
#include <FoundationKitPlatform/Amd64/Clocksource.hpp>
#include <FoundationKitPlatform/Clocksource/TimeKeeper.hpp>
#include <FoundationKitPlatform/Amd64/Cpu.hpp>

using namespace FoundationKitPlatform::Clocksource;
using namespace FoundationKitCxxStl;
using namespace ceryx::proc;
using namespace FoundationKitPlatform::Amd64;

#include <ceryx/fs/pseudo/PseudoFs.hpp>
#include <ceryx/fs/ramfs/RamFs.hpp>
#include <ceryx/fs/CpioLoader.hpp>
#include <ceryx/fs/Vfs.hpp>
#include <ceryx/proc/ElfLoader.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/cpu/Syscall.hpp>
#include <drivers/debugcon.hpp>

// Global VFS Root
FoundationKitCxxStl::RefPtr<ceryx::fs::Vnode> g_vfs_root;

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

    ceryx::cpu::Gdt::Initialize();
    ceryx::cpu::Idt::Initialize();

    arch_cpu_init_per_cpu(&bsp_cpu_data);

    FK_BUG_ON(!(get_memmap_request()->response || get_hhdm_request()->response), "ceryx::start_kernel: no memory map or HHDM found");
    ceryx::mm::MemoryManager::Initialize(get_memmap_request()->response, get_hhdm_request()->response);

    ceryx::cpu::Lapic::Initialize();
    ceryx::cpu::ApicTimer::Initialize();

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

    Scheduler::Initialize();
    Scheduler::Start();

    // VFS Root Genesis
    g_vfs_root = FoundationKitCxxStl::RefPtr<ceryx::fs::Vnode>(
        new ceryx::fs::ramfs::RamFsNode(
            ceryx::fs::VnodeType::Directory,
            ceryx::fs::ramfs::RamFsOpsImpl::GetOps(),
            StringView("/")
        )
    );
    FK_LOG_INFO("ceryx::start_kernel: VFS root directory ('/') instantiated.");

    ceryx::cpu::Syscall::Initialize();
    ceryx::cpu::ApicTimer::Periodic(10000000);

    // Load Initrd
    auto* mod_res = get_module_request()->response;
    if (mod_res && mod_res->module_count > 0) {
        auto* initrd = mod_res->modules[0];
        FK_LOG_INFO("ceryx::start_kernel: loading initrd from module at {:#x} ({} bytes)", 
                    reinterpret_cast<uptr>(initrd->address), initrd->size);
        ceryx::fs::CpioLoader::Populate(g_vfs_root, initrd->address, initrd->size);
    }

#include <ceryx/fs/pseudo/PseudoFs.hpp>
#include <drivers/debugcon.hpp>

    // Launch /sbin/init
    auto init_node_res = ceryx::fs::Vfs::PathToVnode(g_vfs_root, "/sbin/init");
    if (init_node_res) {
        FK_LOG_INFO("ceryx::start_kernel: launching /sbin/init...");
        
        auto proc_res = Process::Create(g_vfs_root, g_vfs_root);
        if (proc_res) {
            auto* proc = proc_res.Value();

            // Setup STDIN (FD 0) and STDOUT (FD 1) -> Debug Console
            auto stdout_vnode = RefPtr<ceryx::fs::Vnode>(
                new ceryx::fs::pseudo::PseudoFsNode(
                    ceryx::fs::pseudo::PseudoFsOpsImpl::GetOps(),
                    "stdout",
                    nullptr, // No read
                    [](const void* buffer, usize size, usize /*offset*/) noexcept -> Expected<usize, int> {
                        const char* data = static_cast<const char*>(buffer);
                        for (usize i = 0; i < size; ++i) {
                            debugcon_putc(data[i]);
                        }
                        return size;
                    }
                )
            );
            
            proc->AllocateFd(RefPtr<ceryx::fs::FileDescription>(new ceryx::fs::FileDescription(stdout_vnode))); // FD 0
            proc->AllocateFd(RefPtr<ceryx::fs::FileDescription>(new ceryx::fs::FileDescription(stdout_vnode))); // FD 1

            auto entry_res = ElfLoader::Load(*proc, *init_node_res.Value());
            if (entry_res) {
                auto* thread = new Thread(proc, false);
                thread->InitializeUserStack(entry_res.Value(), 0x7FFFF0000000ULL);
                Scheduler::AddThread(thread);
                FK_LOG_INFO("ceryx::start_kernel: init process scheduled at RIP {:#x}", entry_res.Value());
            } else {
                FK_LOG_ERR("ceryx::start_kernel: failed to load /sbin/init ELF: error {}", entry_res.Error());
            }
        } else {
             FK_LOG_ERR("ceryx::start_kernel: failed to create init process");
        }
    } else {
        FK_LOG_WARN("ceryx::start_kernel: /sbin/init not found in VFS root.");
    }

    FK_LOG_INFO("ceryx::start_kernel: init done.");
    Sti();

    for (;;) {
        Hlt();
    }
}