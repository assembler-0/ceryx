#include <ceryx/cpu/Syscall.hpp>
#include <ceryx/cpu/Gdt.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>
#include <drivers/debugcon.hpp>
#include <ceryx/proc/Scheduler.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/fs/FileDescription.hpp>
#include <ceryx/Errno.h>

namespace ceryx::cpu {

using namespace FoundationKitPlatform::Amd64;

extern "C" void SyscallEntry();

extern "C" u64 SyscallDispatch(SyscallRegisters* regs) {
    return Syscall::Dispatch(regs);
}

void Syscall::Initialize() {
    // Enable SYSCALL/SYSRET in EFER
    ControlRegs::SetEferBits(static_cast<u64>(ControlRegs::EferFlags::Sce));

    // STAR: 
    // [47:32] -> Kernel CS (SS = CS + 8)
    // [63:48] -> User CS (SS = CS - 8)
    
    // So if we set STAR[63:48] to 0x10 (Kernel Data index), then:
    // SS = 0x10 + 8 = 0x18 (index 3) -> 0x18 | 3 = 0x1B.
    // CS = 0x10 + 16 = 0x20 (index 4) -> 0x20 | 3 = 0x23.
    
    u64 star = (static_cast<u64>(Gdt::kKernelCodeSelector) << 32) |
               (static_cast<u64>(0x10 | 3) << 48); // We use 0x10 as base for user, RPL 3

    ControlRegs::WriteMsr(ControlRegs::kMsrStar, star);

    // LSTAR: Entry point
    ControlRegs::WriteMsr(ControlRegs::kMsrLstar, reinterpret_cast<u64>(SyscallEntry));

    // SFMASK: Mask IF (bit 9), DF (bit 10), TF (bit 8), AC (bit 18)
    ControlRegs::WriteMsr(ControlRegs::kMsrSfmask, 0x40700);
}

u64 Syscall::Dispatch(SyscallRegisters* regs) {
    // Log the syscall for verification.
    // In a real kernel, we would use a more efficient way, but for verification this is perfect.
    FK_LOG_INFO("ceryx::cpu::Syscall: Received syscall {} from RIP {:#x}", regs->rax, regs->rip);
    
    auto* thread = proc::Scheduler::GetCurrentThread();
    auto* process = thread ? thread->GetProcess() : nullptr;

    switch (regs->rax) {
        case 0: { // sys_read
            if (!process) return static_cast<u64>(-ENOSYS);
            auto fd_desc = process->GetFd(regs->rdi);
            if (!fd_desc) return static_cast<u64>(-EBADF);
            auto res = fd_desc->Read(reinterpret_cast<void*>(regs->rsi), regs->rdx);
            if (!res) return static_cast<u64>(res.Error());
            return res.Value();
        }
        case 1: { // sys_write
            if (!process) return static_cast<u64>(-ENOSYS);
            auto fd_desc = process->GetFd(regs->rdi);
            if (!fd_desc) return static_cast<u64>(-EBADF);
            auto res = fd_desc->Write(reinterpret_cast<const void*>(regs->rsi), regs->rdx);
            if (!res) return static_cast<u64>(res.Error());
            return res.Value();
        }
        case 3: { // sys_close
            if (!process) return static_cast<u64>(-ENOSYS);
            process->FreeFd(regs->rdi);
            return 0;
        }
        case 60: { // sys_exit
            if (thread) {
                thread->SetState(proc::ThreadState::Terminated);
                proc::Scheduler::Yield();
            }
            return 0;
        }
        default: break;
    }

    return static_cast<u64>(-ENOSYS);
}

} // namespace ceryx::cpu
