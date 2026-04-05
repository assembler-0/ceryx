#pragma once

#define DEBUGCON_BASE 0xE9

void debugcon_putc(const char c);
void debugcon_puts(const char* s);