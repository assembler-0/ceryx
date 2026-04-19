#include <ceryx/proc/Scheduler.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/cpu/Idt.hpp>
#include <ceryx/cpu/Gdt.hpp>
#include <ceryx/cpu/Lapic.hpp>
#include <ceryx/cpu/CpuData.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Sync/InterruptSafe.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl::Sync;
using namespace FoundationKitCxxStl::Structure;
using namespace FoundationKitPlatform::Amd64;

extern "C" void SwitchContext(u64* prev_rsp, u64 next_rsp);

struct Scheduler::RunQueueImpl {
    IntrusiveDoublyLinkedList list;
};

Scheduler::RunQueueImpl* Scheduler::s_impl = nullptr;
SpinLock Scheduler::s_lock;

static Scheduler::RunQueueImpl g_runqueue_impl;

void idle_func(uptr) {
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void Scheduler::Initialize() {
    s_impl = &g_runqueue_impl;
}

void Scheduler::Start() {
    if (!s_impl) Initialize();

    // Create idle thread for the current CPU
    auto* cpu_data = static_cast<cpu::CpuData*>(FoundationKitOsl::OslGetPerCpuBase());
    
    // We create a "kernel thread" for idle loop. 
    Thread* idle = new Thread(nullptr, true);
    idle->InitializeStack(reinterpret_cast<uptr>(idle_func), 0);
    idle->SetState(ThreadState::Running);
    
    cpu_data->idle_thread = idle;
    cpu_data->current_thread = idle;

    // Update initial kernel stack
    u64 stack_top = idle->GetKernelStack() + 16384;
    cpu::Gdt::SetStack(cpu::PrivilegeLevel::Ring0, stack_top);
    cpu_data->kernel_stack = stack_top;
    
    FK_LOG_INFO("ceryx::proc::Scheduler: Started on CPU {}", cpu_data->cpu_id);
}

void Scheduler::AddThread(Thread* thread) {
    InterruptSafeSpinLock guard(s_lock);
    thread->SetState(ThreadState::Ready);
    s_impl->list.PushBack(&thread->run_node);
}

void Scheduler::Schedule() {
    InterruptSafeSpinLock guard(s_lock);

    auto* cpu_data = static_cast<cpu::CpuData*>(FoundationKitOsl::OslGetPerCpuBase());
    if (!cpu_data) return;

    auto* prev = static_cast<Thread*>(cpu_data->current_thread);
    
    Thread* next = nullptr;
    if (!s_impl->list.Empty()) {
        auto* next_node = s_impl->list.PopFront();
        next = ContainerOf<Thread, &Thread::run_node>(next_node);

        if (prev && prev->State() == ThreadState::Running) {
            prev->SetState(ThreadState::Ready);
            s_impl->list.PushBack(&prev->run_node);
        }
    } else {
        // No threads in runqueue, switch to idle if not already there
        if (cpu_data->idle_thread && prev != cpu_data->idle_thread) {
            next = static_cast<Thread*>(cpu_data->idle_thread);
            if (prev && prev->State() == ThreadState::Running) {
                prev->SetState(ThreadState::Ready);
                s_impl->list.PushBack(&prev->run_node);
            }
        }
    }

    if (next && next != prev) {
        cpu_data->current_thread = next;
        next->SetState(ThreadState::Running);

        // Update TSS and CpuData with the new thread's kernel stack.
        // This is crucial for Ring 3 -> Ring 0 transitions (interrupts and syscalls).
        u64 stack_top = next->GetKernelStack() + 16384;
        cpu::Gdt::SetStack(cpu::PrivilegeLevel::Ring0, stack_top);
        cpu_data->kernel_stack = stack_top;

        // Address Space Switching (CR3)
        // If the new thread belongs to a process, switch to its address space.
        // If it's a kernel thread (process == null), it must use the kernel's 
        // root page tables.
        PhysicalAddress root_pa;
        if (next->GetProcess()) {
            root_pa = next->GetProcess()->GetAddressSpace().GetRootPa();
        } else {
            root_pa = mm::MemoryManager::GetPtm().RootPhysicalAddress();
        }

        if (ControlRegs::ReadCr3() != root_pa.value) {
            ControlRegs::WriteCr3(root_pa.value);
        }

        if (prev) {
            SwitchContext(&prev->Registers().rsp, next->Registers().rsp);
        } else {
            static u64 dummy_rsp;
            SwitchContext(&dummy_rsp, next->Registers().rsp);
        }
    }
}

Thread* Scheduler::GetCurrentThread() {
    auto* cpu_data = static_cast<cpu::CpuData*>(FoundationKitOsl::OslGetPerCpuBase());
    return static_cast<Thread*>(cpu_data->current_thread);
}

void Scheduler::Yield() {
    Schedule();
}

} // namespace ceryx::proc
