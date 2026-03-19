#include <ceryx/base.h>

void start_kernel() {
	for (;;) {
		__asm__ volatile("hlt");
	}
}
