#include <FoundationKitCxxStl/Base/Version.hpp>
#include <lib/linearfb.hpp>
#include <ceryx/mm/MemoryManager.hpp>
#include <arch/x86_64/boot/requests.hpp>

extern "C" void start_kernel()
{
    linearfb_console_init();
    FK_LOG_INFO("welcome to ceryx!");
    FoundationKitCxxStl::PrintFoundationKitInfo();

    // Initialize Memory Management
    FK_BUG_ON(!(get_memmap_request()->response || get_hhdm_request()->response), "ceryx::start_kernel: no memory map or HHDM found.");
    ceryx::mm::MemoryManager::Initialize(get_memmap_request()->response, get_hhdm_request()->response);

    FK_LOG_INFO("ceryx::start_kernel: init done.");

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}