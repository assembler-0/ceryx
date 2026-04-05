#include <drivers/debugcon.hpp>
#include <arch/x86_64/pio.hpp>

void debugcon_putc(const char c) {
    ceryx::arch::x86_64::pio::outb(DEBUGCON_BASE, c);
}

void debugcon_puts(const char* s) {
    do {
        debugcon_putc(*s);
    } while (*s++);
}