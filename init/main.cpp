#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <lib/linearfb.hpp>

extern "C" void start_kernel() {
    linearfb_console_init();
    FK_LOG_INFO("ceryx (R) - FoundationKit demonstration kernel.");
    FK_LOG_INFO("ceryx: entering bootstrap");
    FK_LOG_INFO("FoundationKit: Subsystem discovery starting...");

    for (;;) {
        __asm__ volatile("hlt");
    }
}
