#include <FoundationKitOsl/Osl.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <ceryx/cpu/CpuData.hpp>
#include <ceryx/cpu/Lapic.hpp>
#include <ceryx/proc/Scheduler.hpp>
#include <ceryx/proc/Thread.hpp>
#include <lib/linearfb.hpp>
#include <drivers/debugcon.hpp>
#include <FoundationKitPlatform/Amd64/Cpu.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitPlatform::Amd64;

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
            // Check CR4.OSFXSR and CR4.OSXMMEXCPT
            u64 cr4 = ControlRegs::ReadCr4();
            return (cr4 & (1ULL << 9)) && (cr4 & (1ULL << 10));
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
            // For now, we only support a single CPU (BSP).
            if (cpu_id == 0) return OslGetPerCpuBase();
            return nullptr;
        }

        u64 OslGetSystemTicks() {
            return Rdtsc();
        }

        u64 OslGetSystemFrequency() {
            return ceryx::cpu::ApicTimer::GetTscFrequency();
        }

        u64 OslGetWallClockBase() {
            // TODO: Implement RTC or EFI time lookup.
            return 0;
        }

        void OslMicroDelay(u64 microseconds) {
            u64 start = Rdtsc();
            u64 delta = (microseconds * ceryx::cpu::ApicTimer::GetTscFrequency()) / 1000000;
            while (Rdtsc() - start < delta) {
                __asm__ volatile("pause");
            }
        }

        u64 OslGetCurrentThreadId() {
            auto* thread = ceryx::proc::Scheduler::GetCurrentThread();
            if (!thread) return 0;
            return thread->GetId();
        }

        void OslThreadYield() {
            ceryx::proc::Scheduler::Yield();
        }

        void OslThreadSleep(void* channel) {
            // TODO: Implement sleep/wait mechanism
        }

        void OslThreadWake(void* channel) {
            // TODO: Implement wake mechanism
        }

        void OslThreadWakeAll(void* channel) {
            // TODO: Implement wake-all mechanism
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
            __asm__ volatile(
                "push %0\n"
                "popfq\n"
                : : "r"(state) : "memory", "cc"
            );
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