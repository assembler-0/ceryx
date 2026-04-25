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
#include <ceryx/fs/Stat.hpp>
#include <ceryx/proc/ElfLoader.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/proc/Signal.hpp>
#include <ceryx/cpu/Syscall.hpp>
#include <ceryx/fs/pseudo/PseudoFs.hpp>
#include <drivers/debugcon.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>

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

    // ── /dev ─────────────────────────────────────────────────────────────────
    // Create /dev directory and populate standard character devices.
    {
        auto dev_res = g_vfs_root->ops->Mkdir(*g_vfs_root, "dev");
        FK_BUG_ON(!dev_res, "ceryx::start_kernel: failed to create /dev");
        auto& dev_dir = *static_cast<ceryx::fs::ramfs::RamFsNode*>(dev_res.Value().Get());

        // /dev/null — reads return 0 bytes, writes are silently discarded
        auto null_node = RefPtr<ceryx::fs::pseudo::PseudoFsNode>(
            new ceryx::fs::pseudo::PseudoFsNode(
                ceryx::fs::pseudo::PseudoFsOpsImpl::GetOps(),
                "null",
                [](void* /*buf*/, usize /*size*/, usize /*off*/) noexcept
                    -> FoundationKitCxxStl::Expected<usize, int> { return usize{0}; },
                [](const void* /*buf*/, usize size, usize /*off*/) noexcept
                    -> FoundationKitCxxStl::Expected<usize, int> { return size; }
            ));
        null_node->type = ceryx::fs::VnodeType::CharDevice;
        dev_dir.InsertChild(RefPtr<ceryx::fs::Vnode>(null_node));

        // /dev/zero — reads return zero-filled bytes, writes are discarded
        auto zero_node = RefPtr<ceryx::fs::pseudo::PseudoFsNode>(
            new ceryx::fs::pseudo::PseudoFsNode(
                ceryx::fs::pseudo::PseudoFsOpsImpl::GetOps(),
                "zero",
                [](void* buf, usize size, usize /*off*/) noexcept
                    -> FoundationKitCxxStl::Expected<usize, int> {
                    FoundationKitMemory::MemoryZero(buf, size);
                    return size;
                },
                [](const void* /*buf*/, usize size, usize /*off*/) noexcept
                    -> FoundationKitCxxStl::Expected<usize, int> { return size; }
            ));
        zero_node->type = ceryx::fs::VnodeType::CharDevice;
        dev_dir.InsertChild(RefPtr<ceryx::fs::Vnode>(zero_node));

        // /dev/tty — writes go to framebuffer + debugcon, reads return EOF
        auto tty_node = RefPtr<ceryx::fs::pseudo::PseudoFsNode>(
            new ceryx::fs::pseudo::PseudoFsNode(
                ceryx::fs::pseudo::PseudoFsOpsImpl::GetOps(),
                "tty",
                [](void* /*buf*/, usize /*size*/, usize /*off*/) noexcept
                    -> FoundationKitCxxStl::Expected<usize, int> { return usize{0}; },
                [](const void* buffer, usize size, usize /*off*/) noexcept
                    -> FoundationKitCxxStl::Expected<usize, int> {
                    const char* data = static_cast<const char*>(buffer);
                    for (usize i = 0; i < size; ++i) {
                        linearfb_console_putc(data[i]);
                        debugcon_putc(data[i]);
                    }
                    return size;
                }
            ));
        tty_node->type = ceryx::fs::VnodeType::CharDevice;
        dev_dir.InsertChild(RefPtr<ceryx::fs::Vnode>(tty_node));

        FK_LOG_INFO("ceryx::start_kernel: /dev populated (null, zero, tty).");
    }

    // ── /proc ─────────────────────────────────────────────────────────────────
    // Minimal procfs: /proc/version for now.
    {
        auto proc_res = g_vfs_root->ops->Mkdir(*g_vfs_root, "proc");
        FK_BUG_ON(!proc_res, "ceryx::start_kernel: failed to create /proc");
        auto& proc_dir = *static_cast<ceryx::fs::ramfs::RamFsNode*>(proc_res.Value().Get());

        // /proc/version — Linux-compatible version string
        auto version_node = RefPtr<ceryx::fs::pseudo::PseudoFsNode>(
            new ceryx::fs::pseudo::PseudoFsNode(
                ceryx::fs::pseudo::PseudoFsOpsImpl::GetOps(),
                "version",
                [](void* buf, usize size, usize off) noexcept
                    -> FoundationKitCxxStl::Expected<usize, int> {
                    static constexpr char kVersion[] =
                        "Linux version 6.1.0 (ceryx-kernel) (clang) #1\n";
                    static constexpr usize kLen = sizeof(kVersion) - 1;
                    if (off >= kLen) return usize{0};
                    usize to_copy = kLen - off;
                    if (to_copy > size) to_copy = size;
                    FoundationKitMemory::MemoryCopy(buf,
                        reinterpret_cast<const void*>(kVersion + off), to_copy);
                    return to_copy;
                },
                nullptr
            ));
        proc_dir.InsertChild(RefPtr<ceryx::fs::Vnode>(version_node));

        FK_LOG_INFO("ceryx::start_kernel: /proc populated (version).");
    }

    // ── Standard directories ──────────────────────────────────────────────────
    // Pre-create so the initrd CPIO can populate them without needing mkdir.
    g_vfs_root->ops->Mkdir(*g_vfs_root, "sbin");
    g_vfs_root->ops->Mkdir(*g_vfs_root, "bin");
    g_vfs_root->ops->Mkdir(*g_vfs_root, "tmp");
    g_vfs_root->ops->Mkdir(*g_vfs_root, "etc");

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

    // Launch /sbin/init
    auto init_node_res = ceryx::fs::Vfs::PathToVnode(g_vfs_root, "/sbin/init");
    if (init_node_res) {
        FK_LOG_INFO("ceryx::start_kernel: launching /sbin/init...");
        
        auto init_spawn_res = ceryx::proc::Process::Spawn(g_vfs_root, "/sbin/init");
        if (init_spawn_res) {
            auto* init_proc = init_spawn_res.Value();
            
            // Create stdout pseudo-node for the terminal
            // Writes go to both the framebuffer console and the debug port so
            // output is visible regardless of which output path is active.
            auto stdout_vnode = RefPtr<ceryx::fs::Vnode>(new ceryx::fs::pseudo::PseudoFsNode(
                ceryx::fs::pseudo::PseudoFsOpsImpl::GetOps(),
                "stdout",
                nullptr, // No read on stdout
                [](const void* buffer, usize size, usize /*offset*/) noexcept -> Expected<usize, int> {
                    const char* data = static_cast<const char*>(buffer);
                    for (usize i = 0; i < size; ++i) {
                        linearfb_console_putc(data[i]);
                        debugcon_putc(data[i]);
                    }
                    return size;
                }
            ));

            // Setup standard FDs for init
            init_proc->AllocateFd(RefPtr<ceryx::fs::FileDescription>(new ceryx::fs::FileDescription(stdout_vnode))); // FD 0
            init_proc->AllocateFd(RefPtr<ceryx::fs::FileDescription>(new ceryx::fs::FileDescription(stdout_vnode))); // FD 1
            init_proc->AllocateFd(RefPtr<ceryx::fs::FileDescription>(new ceryx::fs::FileDescription(stdout_vnode))); // FD 2
        } else {
             FK_LOG_ERR("ceryx::start_kernel: failed to spawn /sbin/init: error {}", init_spawn_res.Error());
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