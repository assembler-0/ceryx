#include <ceryx/proc/Thread.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/mm/MemoryManager.hpp>
#include <FoundationKitMemory/Management/KernelMemoryManager.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace ceryx::proc {

extern "C" void ThreadTrampoline();
extern "C" void UserspaceTrampoline();
extern "C" void ForkReturn();

u64 Thread::s_next_tid = 1;

Thread::Thread(Process* process, bool is_kernel) noexcept
    : m_process(process)
    , m_tid(s_next_tid++)
    , m_state(ThreadState::Ready)
{
    // Allocate kernel stack (32KB)
    constexpr usize kStackSize = 32768;
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

void Thread::InitializeForkStack(const cpu::InterruptFrame& parent_frame) noexcept {
    // Push a complete InterruptFrame onto the child's kernel stack so that
    // ForkReturn can iretq directly into userspace with rax=0.
    // InterruptFrame field order (struct layout, low addr first):
    //   r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax,
    //   interrupt_number, error_code, rip, cs, rflags, rsp(user), ss
    // The stack grows DOWN, so we push in REVERSE order (ss first).

    constexpr usize kStackSize = 32768;
    u64* kstack = reinterpret_cast<u64*>(m_kernel_stack + kStackSize);

    // ── 1. IRET frame (pushed first = highest stack address) ─────────────────
    *(--kstack) = parent_frame.ss;
    *(--kstack) = parent_frame.rsp;     // user RSP
    *(--kstack) = parent_frame.rflags;
    *(--kstack) = parent_frame.cs;
    *(--kstack) = parent_frame.rip;     // child resumes at same RIP as parent

    // ── 2. Dummy metadata QWORDS ──────────────────────────────────────────────
    *(--kstack) = 0ULL;                 // error_code
    *(--kstack) = 0ULL;                 // interrupt_number

    // ── 3. General-purpose registers (rax=0 → fork returns 0 in child) ───────
    *(--kstack) = 0ULL;                 // rax = 0
    *(--kstack) = parent_frame.rbx;
    *(--kstack) = parent_frame.rcx;
    *(--kstack) = parent_frame.rdx;
    *(--kstack) = parent_frame.rsi;
    *(--kstack) = parent_frame.rdi;
    *(--kstack) = parent_frame.rbp;
    *(--kstack) = parent_frame.r8;
    *(--kstack) = parent_frame.r9;
    *(--kstack) = parent_frame.r10;
    *(--kstack) = parent_frame.r11;
    *(--kstack) = parent_frame.r12;
    *(--kstack) = parent_frame.r13;
    *(--kstack) = parent_frame.r14;
    *(--kstack) = parent_frame.r15;
    // kstack now points to InterruptFrame.r15 — the top of the frame.

    // ── 4. SwitchContext callee-save frame (6 regs + ForkReturn ret addr) ────
    //    SwitchContext pops: r15, r14, r13, r12, rbx, rbp  then  ret
    *(--kstack) = reinterpret_cast<u64>(ForkReturn); // ret → ForkReturn
    *(--kstack) = 0ULL;  // rbp
    *(--kstack) = 0ULL;  // rbx
    *(--kstack) = 0ULL;  // r12
    *(--kstack) = 0ULL;  // r13
    *(--kstack) = 0ULL;  // r14
    *(--kstack) = 0ULL;  // r15

    // ── 5. Save kernel RSP into m_regs.rsp (used by SwitchContext) ───────────
    m_regs.rsp = reinterpret_cast<u64>(kstack);
}

void Thread::SetName(const char* name) noexcept {
    if (!name) {
        m_name[0] = '\0';
        return;
    }
    usize i = 0;
    while (i < sizeof(m_name) - 1 && name[i] != '\0') {
        m_name[i] = name[i];
        ++i;
    }
    m_name[i] = '\0';
}

Thread::~Thread() noexcept {
    if (m_kernel_stack != 0) {
        // Free kernel stack (32KB = 8 pages)
        mm::MemoryManager::GetKmm().FreeKernel(FoundationKitMemory::VirtualAddress{m_kernel_stack}, 8);
        m_kernel_stack = 0;
    }
}

extern "C" void ThreadDestroy(Thread* thread) {
    FK_LOG_INFO("ceryx::proc: Thread {} terminated.", thread->GetId());
    for (;;) { __asm__ volatile("cli; hlt"); }
}

} // namespace ceryx::proc
