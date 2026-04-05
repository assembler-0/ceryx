#include <FoundationKitOsl/Osl.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitMemory/GlobalAllocator.hpp>

#include "drivers/debugcon.hpp"

using namespace FoundationKitCxxStl;

extern "C" {
    // 3. Basic memory functions (Compiler emits calls to these)
    void* memset(void* dest, int ch, usize count) {
        auto* ptr = static_cast<unsigned char*>(dest);
        while (count--) {
            *ptr++ = static_cast<unsigned char>(ch);
        }
        return dest;
    }

    void* memcpy(void* dest, const void* src, usize count) {
        auto* d = static_cast<unsigned char*>(dest);
        const auto* s = static_cast<const unsigned char*>(src);
        while (count--) {
            *d++ = *s++;
        }
        return dest;
    }
}

// 4. Global sized delete operator (C++14)
// Removed because FoundationKitCxxAbi/Src/OperatorNew.cpp already defines it.
#if 0
void operator delete(void* ptr, usize size) {
    ::operator delete(ptr);
}
#endif

namespace FoundationKitOsl {

    extern "C" {

        [[noreturn]] void OslBug(const char* msg) {
            debugcon_puts(msg);
            for (;;) {
                __asm__ volatile("hlt");
            }
        }

        void OslLog(const char* msg) {
            debugcon_puts(msg);
        }

        bool OslIsSimdEnabled() {
            return false;
        }

        u64 OslGetCurrentThreadId() {
            return 1;
        }

        void OslThreadYield() {
        }

        void OslThreadSleep(void* channel) {
        }

        void OslThreadWake(void* channel) {
        }

        void OslThreadWakeAll(void* channel) {
        }

        uptr OslInterruptDisable() {
            uptr flags;
            __asm__ volatile(
                "pushfq\n"
                "popq %0\n"
                "cli\n"
                : "=r"(flags) :: "memory"
            );
            return flags;
        }

        void OslInterruptRestore(uptr state) {
            if (state & (1ULL << 9)) {  // check IF bit
                __asm__ volatile("sti" ::: "memory");
            }
        }

        bool OslIsInterruptEnabled() {
            uptr flags;
            __asm__ volatile("pushfq; popq %0" : "=r"(flags));
            return (flags & (1ULL << 9)) != 0;
        }

    } // extern "C"
} // namespace FoundationKitOsl