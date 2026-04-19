#include <FoundationKitOsl/Osl.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <ceryx/cpu/CpuData.hpp>
#include <lib/linearfb.hpp>
#include <drivers/debugcon.hpp>

using namespace FoundationKitCxxStl;

extern "C" {
    // 3. Basic memory functions (Compiler emits calls to these)
    void* memset(void* dest, int ch, usize count) {
        return FoundationKitMemory::MemorySet(dest, ch, count);
    }

    void* memcpy(void* dest, const void* src, usize count) {
        return FoundationKitMemory::MemoryCopy(dest, src, count);
    }

    void* memmove(void* dest, const void* src, usize size) {
        return FoundationKitMemory::MemoryMove(dest, src, size);
    }
}

namespace FoundationKitOsl {

    extern "C" {

        [[noreturn]] void OslBug(const char* msg) {
            FK_LOG_INFO("ceryx: kernel panic:");
            OslLog(msg);
            for (;;) { __asm__ volatile("cli; hlt"); }
        }

        void OslLog(const char* msg) {
            linearfb_console_puts(msg);
            debugcon_puts(msg);
        }

        bool OslIsSimdEnabled() {
            return false;
        }

        u32 OslGetCurrentCpuId() {
            auto* cpu_data = static_cast<ceryx::cpu::CpuData*>(OslGetPerCpuBase());
            if (!cpu_data) return 0;
            return cpu_data->cpu_id;
        }

        void* OslGetPerCpuBase() {
            void* base;
            __asm__ volatile("movq %%gs:0, %0" : "=r"(base));
            return base;
        }

        void* OslGetPerCpuBaseFor(u32 cpu_id) {
            // TODO: Implement a way to look up Per-CPU base by ID.
            // For now, only the current CPU is supported.
            auto current_id = OslGetCurrentCpuId();
            if (cpu_id == current_id) return OslGetPerCpuBase();
            return nullptr;
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

        static char hostname[16] = "ceryx 0.01a";

        char* OslGetHostOsName() {
            return hostname;
        }

    } // extern "C"
} // namespace FoundationKitOsl