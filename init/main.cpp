#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Version.hpp>
#include <FoundationKitPlatform/Amd64/Cpu.hpp>
#include <lib/linearfb.hpp>

extern "C" void start_kernel() {
    linearfb_console_init();
    FoundationKitOsl::OslLog("welcome my first C++ kernel - ceryx!\n");
    FoundationKitOsl::OslLog("starting FoundationKit...\n");
    FoundationKitCxxStl::PrintFoundationKitInfo();

    for (;;) {
        __asm__ volatile("hlt");
    }
}
