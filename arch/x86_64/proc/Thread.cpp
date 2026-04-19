#include <ceryx/proc/Thread.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/mm/MemoryManager.hpp>
#include <FoundationKitMemory/Management/KernelMemoryManager.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace ceryx::proc {

extern "C" void ThreadTrampoline();
extern "C" void UserspaceTrampoline();

u64 Thread::s_next_tid = 1;

Thread::Thread(Process* process, bool is_kernel) noexcept
    : m_process(process)
    , m_tid(s_next_tid++)
    , m_state(ThreadState::Ready)
{
    // Allocate kernel stack (16KB)
    constexpr usize kStackSize = 16384;
    constexpr usize kStackPages = kStackSize / 4096;

    auto stack_res = mm::MemoryManager::GetKmm().AllocateKernel(kStackPages, 
                        FoundationKitMemory::RegionFlags::Writable | FoundationKitMemory::RegionFlags::Readable, 
                        FoundationKitMemory::RegionType::Generic);
    
    FK_BUG_ON(!stack_res.HasValue(), "Thread: failed to allocate kernel stack");
    
    m_kernel_stack = stack_res.Value().value;
    
    // Preliminary stack setup: set RSP to the top of the allocated stack.
    m_regs.rsp = m_kernel_stack + kStackSize;
}

void Thread::InitializeStack(uptr entry_point, uptr arg) noexcept {
    u64* stack_ptr = reinterpret_cast<u64*>(m_regs.rsp);
    
    // 1. Push landing site for SwitchContext's 'ret'
    *(--stack_ptr) = reinterpret_cast<u64>(ThreadTrampoline);
    
    // 2. Initialize preserved registers (Matches SwitchContext pop order)
    *(--stack_ptr) = 0; // rbp
    *(--stack_ptr) = 0; // rbx
    *(--stack_ptr) = entry_point; // r12
    *(--stack_ptr) = arg;         // r13
    *(--stack_ptr) = reinterpret_cast<u64>(this); // r14
    *(--stack_ptr) = 0; // r15
    
    m_regs.rsp = reinterpret_cast<u64>(stack_ptr);
}

void Thread::InitializeUserStack(uptr user_rip, uptr user_rsp) noexcept {
    u64* stack_ptr = reinterpret_cast<u64*>(m_regs.rsp);

    // 1. Push landing site for SwitchContext's 'ret'
    *(--stack_ptr) = reinterpret_cast<u64>(UserspaceTrampoline);

    // 2. Initialize preserved registers for UserspaceTrampoline
    *(--stack_ptr) = 0; // rbp
    *(--stack_ptr) = 0; // rbx
    *(--stack_ptr) = user_rip; // r12
    *(--stack_ptr) = user_rsp; // r13
    *(--stack_ptr) = 0; // r14
    *(--stack_ptr) = 0; // r15
    
    m_regs.rsp = reinterpret_cast<u64>(stack_ptr);
}

Thread::~Thread() noexcept {
    // TODO: Free kernel stack
}

extern "C" void ThreadDestroy(Thread* thread) {
    FK_LOG_INFO("ceryx::proc: Thread {} terminated.", thread->GetId());
    for (;;) { __asm__ volatile("cli; hlt"); }
}

} // namespace ceryx::proc
