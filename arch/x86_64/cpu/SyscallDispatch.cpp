#include <ceryx/cpu/Syscall.hpp>
#include <ceryx/cpu/Gdt.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <drivers/debugcon.hpp>
#include <ceryx/proc/Scheduler.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/proc/ElfLoader.hpp>
#include <ceryx/proc/Reaper.hpp>
#include <ceryx/fs/FileDescription.hpp>
#include <ceryx/fs/Vfs.hpp>
#include <ceryx/fs/pipe/PipeVnode.hpp>
#include <ceryx/Errno.h>

namespace ceryx::cpu {

using namespace FoundationKitPlatform::Amd64;

extern "C" void SyscallEntry();

extern "C" u64 SyscallDispatch(InterruptFrame* regs) {
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

u64 Syscall::Dispatch(InterruptFrame* regs) {
    using i64 = FoundationKitCxxStl::i64;
    
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
        case 9: { // sys_mmap
            if (!process) return static_cast<u64>(-ENOSYS);
            u64 addr = regs->rdi;
            u64 len = regs->rsi;
            u64 prot = regs->rdx;
            u64 flags = regs->r10;
            // int fd = (int)regs->r8;
            // off_t off = (off_t)regs->r9;

            if (len == 0) return static_cast<u64>(-EINVAL);
            u64 aligned_len = (len + 0xFFF) & ~0xFFFULL;

            FoundationKitMemory::VmaProt vma_prot = FoundationKitMemory::VmaProt::User;
            if (prot & 0x1) vma_prot = vma_prot | FoundationKitMemory::VmaProt::Read;
            if (prot & 0x2) vma_prot = vma_prot | FoundationKitMemory::VmaProt::Write;
            if (prot & 0x4) vma_prot = vma_prot | FoundationKitMemory::VmaProt::Execute;

            FoundationKitMemory::VmaFlags vma_flags = FoundationKitMemory::VmaFlags::None;
            if (flags & 0x01) vma_flags = vma_flags | FoundationKitMemory::VmaFlags::Shared;
            if (flags & 0x02) vma_flags = vma_flags | FoundationKitMemory::VmaFlags::Private;
            if (flags & 0x10) vma_flags = vma_flags | FoundationKitMemory::VmaFlags::Fixed;
            if (flags & 0x20) vma_flags = vma_flags | FoundationKitMemory::VmaFlags::Anonymous;

            if (!HasVmaFlag(vma_flags, FoundationKitMemory::VmaFlags::Anonymous)) {
                return static_cast<u64>(-ENODEV); // File-backed mmap not implemented yet
            }

            auto res = process->GetAddressSpace().GetInner().MapAnonymous(
                FoundationKitMemory::VirtualAddress{addr},
                aligned_len,
                vma_prot,
                vma_flags
            );

            if (!res) return static_cast<u64>(-ENOMEM);
            return res.Value().value;
        }
        case 11: { // sys_munmap
            if (!process) return static_cast<u64>(-ENOSYS);
            u64 addr = regs->rdi;
            u64 len = regs->rsi;
            if (len == 0 || (addr & 0xFFF)) return static_cast<u64>(-EINVAL);
            u64 aligned_len = (len + 0xFFF) & ~0xFFFULL;

            auto res = process->GetAddressSpace().GetInner().Unmap(
                FoundationKitMemory::VirtualAddress{addr},
                aligned_len
            );
            if (!res) return static_cast<u64>(-EINVAL);
            return 0;
        }
        case 3: { // sys_close
            if (!process) return static_cast<u64>(-ENOSYS);
            process->FreeFd(regs->rdi);
            return 0;
        }
        case 12: { // sys_brk
            if (!process) return 0;
            u64 new_brk = regs->rdi;
            u64 current_brk = process->GetHeapEnd();
            u64 heap_start = process->GetHeapStart();

            if (new_brk == 0 || new_brk < heap_start) {
                return current_brk;
            }

            if (new_brk > current_brk) {
                // Align to page size for VMM
                u64 aligned_current = (current_brk + 0xFFF) & ~0xFFFULL;
                u64 aligned_new = (new_brk + 0xFFF) & ~0xFFFULL;

                if (aligned_new > aligned_current) {
                    FK_LOG_INFO("sys_brk: expanding vma at {:#x} (size {:#x})", (uptr)aligned_current, (uptr)(aligned_new - aligned_current));
                    auto res = process->GetAddressSpace().GetInner().MapAnonymous(
                        FoundationKitMemory::VirtualAddress{aligned_current},
                        aligned_new - aligned_current,
                        FoundationKitMemory::VmaProt::UserReadWrite,
                        FoundationKitMemory::VmaFlags::Private | FoundationKitMemory::VmaFlags::Anonymous | FoundationKitMemory::VmaFlags::Fixed
                    );
                    if (!res) {
                        FK_LOG_ERR("sys_brk: MapAnonymous failed: error {}", (int)res.Error());
                        return current_brk;
                    }
                }
            } else if (new_brk < current_brk) {
                u64 aligned_current = (current_brk + 0xFFF) & ~0xFFFULL;
                u64 aligned_new = (new_brk + 0xFFF) & ~0xFFFULL;

                if (aligned_new < aligned_current) {
                    (void)process->GetAddressSpace().GetInner().Unmap(
                        FoundationKitMemory::VirtualAddress{aligned_new},
                        aligned_current - aligned_new
                    );
                }
            }

            process->SetHeap(heap_start, new_brk);
            return new_brk;
        }
        case 59: { // sys_execve
            if (!process || !thread) return static_cast<u64>(-ENOSYS);
            const char* user_path = reinterpret_cast<const char*>(regs->rdi);

            auto vnode_res = fs::Vfs::PathToVnode(process->GetRoot(), user_path);
            if (!vnode_res) return static_cast<u64>(vnode_res.Error());
            auto vnode = vnode_res.Value();

            auto new_uas_res = mm::UserAddressSpace::Create();
            if (!new_uas_res) return static_cast<u64>(-ENOMEM);
            // Use full namespaces to avoid issues with local variable 'thread' or 'process' shadowing
            auto* new_uas = new_uas_res.Value();

            auto* old_uas = process->SetAddressSpace(new_uas);
            
            auto prev_cr3 = FoundationKitPlatform::Amd64::ControlRegs::ReadCr3();
            FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(new_uas->GetRootPa().value);

            auto load_res = ceryx::proc::ElfLoader::Load(*process, *vnode);
            if (!load_res) {
                (void)process->SetAddressSpace(old_uas);
                FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(prev_cr3);
                new_uas->Destroy();
                return static_cast<u64>(load_res.Error());
            }

            auto result = load_res.Value();
            process->SetHeap(result.heap_base, result.heap_base);

            // Successfully loaded new executable. Destroy the old address space.
            old_uas->Destroy(); 

            // Update thread registers for the new entry point.
            regs->rip = result.entry_point;
            regs->rsp = result.stack_top;

            return 0; 
        }
        case 22: { // sys_pipe
            if (!process) return static_cast<u64>(-ENOSYS);
            int* fds = reinterpret_cast<int*>(regs->rdi);

            // 1. Create the Pipe Vnode
            auto vnode_res = fs::PipeVnode::Create();
            if (!vnode_res) return static_cast<u64>(-ENOMEM);
            auto vnode = vnode_res.Value();

            // 2. Create File Descriptions for both ends
            auto rd_desc = RefPtr<fs::FileDescription>(new fs::FileDescription(vnode, fs::PipeVnode::kPipeRead));
            auto wr_desc = RefPtr<fs::FileDescription>(new fs::FileDescription(vnode, fs::PipeVnode::kPipeWrite));

            // 3. Allocate FDs in the process
            auto fd1 = process->AllocateFd(rd_desc);
            auto fd2 = process->AllocateFd(wr_desc);

            if (!fd1 || !fd2) {
                // Cleanup would happen automatically via RefPtr if we returned here, 
                // but we should probably free the FDs if one succeeded.
                if (fd1) process->FreeFd(fd1.Value());
                if (fd2) process->FreeFd(fd2.Value());
                return static_cast<u64>(-ENOMEM);
            }

            // 4. Write back to userspace
            fds[0] = static_cast<int>(fd1.Value());
            fds[1] = static_cast<int>(fd2.Value());

            return 0;
        }
        case 13: { // rt_sigaction
            int sig = static_cast<int>(regs->rdi);
            const proc::sigaction* act = reinterpret_cast<const proc::sigaction*>(regs->rsi);
            proc::sigaction* oact = reinterpret_cast<proc::sigaction*>(regs->rdx);

            if (sig < 1 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) {
                return static_cast<u64>(-EINVAL);
            }

            if (oact) {
                *oact = process->GetSignalAction(sig);
            }

            if (act) {
                process->GetSignalAction(sig) = *act;
            }

            return 0;
        }
        case 14: { // rt_sigprocmask
            int how = static_cast<int>(regs->rdi);
            const proc::sigset_t* set = reinterpret_cast<const proc::sigset_t*>(regs->rsi);
            proc::sigset_t* oset = reinterpret_cast<proc::sigset_t*>(regs->rdx);

            if (oset) {
                *oset = thread->BlockedSignals();
            }

            if (set) {
                proc::sigset_t val = *set;
                val &= ~( (1ULL << (SIGKILL - 1)) | (1ULL << (SIGSTOP - 1)) );

                switch (how) {
                    case 0: // SIG_BLOCK
                        thread->BlockedSignals() |= val;
                        break;
                    case 1: // SIG_UNBLOCK
                        thread->BlockedSignals() &= ~val;
                        break;
                    case 2: // SIG_SETMASK
                        thread->BlockedSignals() = val;
                        break;
                    default:
                        return static_cast<u64>(-EINVAL);
                }
            }

            return 0;
        }
        case 15: { // rt_sigreturn
            // The user RSP at syscall entry is stored in gs:24
            u64 user_rsp = 0;
            __asm__ volatile ("mov %%gs:24, %0" : "=r"(user_rsp));

            // At the point of rt_sigreturn, user_rsp points to sigframe
            const proc::sigframe* uframe = reinterpret_cast<const proc::sigframe*>(user_rsp);
            const auto& m = uframe->uc.uc_mcontext;

            // Restore registers
            regs->r15 = m.r15; regs->r14 = m.r14; regs->r13 = m.r13; regs->r12 = m.r12;
            regs->r11 = m.r11; regs->r10 = m.r10; regs->r9  = m.r9;  regs->r8  = m.r8;
            regs->rbp = m.rbp; regs->rdi = m.rdi; regs->rsi = m.rsi; regs->rdx = m.rdx;
            regs->rcx = m.rcx; regs->rbx = m.rbx; regs->rax = m.rax;
            regs->rsp = m.rsp;
            regs->rip = m.rip;
            regs->rflags = m.eflags;
            regs->cs = m.cs; // Ensure CS is restored if it changed (though rare in simple rt_sigreturn)

            // Restore signal mask
            thread->BlockedSignals() = uframe->uc.uc_sigmask;

            // Return rax so it isn't overwritten by syscall dispatch return
            return regs->rax;
        }
        case 39: { // sys_getpid
            auto* proc = thread ? thread->GetProcess() : nullptr;
            return proc ? static_cast<u64>(proc->GetPid()) : 0;
        }
        case 57: { // sys_fork
            auto* proc = thread ? thread->GetProcess() : nullptr;
            if (!proc) return static_cast<u64>(-EINVAL);

            auto child_res = proc->Fork(regs);
            if (!child_res.HasValue()) return static_cast<u64>(child_res.Error());

            // Parent returns child PID; child gets 0 via ForkReturn trampoline.
            return static_cast<u64>(child_res.Value()->GetPid());
        }
        case 60: { // sys_exit / sys_exit_group
            int status = static_cast<int>(regs->rdi);
            auto* proc = thread ? thread->GetProcess() : nullptr;
            if (proc) {
                // Mark process as zombie + wake any waiting parent.
                proc->Exit(status);
            }
            if (thread) {
                thread->SetState(ceryx::proc::ThreadState::Terminated);
                ceryx::proc::Reaper::Enqueue(thread);
                ceryx::proc::Scheduler::Yield(); // Never returns.
            }
            return 0;
        }
        case 61: { // sys_waitpid
            constexpr int WNOHANG = 1;
            i64   wpid    = static_cast<i64>(regs->rdi);
            int*  wstatus = reinterpret_cast<int*>(regs->rsi);
            int   options = static_cast<int>(regs->rdx);

            auto* proc = thread ? thread->GetProcess() : nullptr;
            if (!proc) return static_cast<u64>(-EINVAL);

            // Find target child (pid==0 or pid==-1 means any child).
            u64 search_pid = (wpid <= 0) ? 0 : static_cast<u64>(wpid);
            ceryx::proc::Process* child = proc->FindChild(search_pid);
            if (!child) return static_cast<u64>(-ECHILD);

            if (!child->IsZombie()) {
                if (options & WNOHANG) return 0;
                // Block on the child Process* as the wait channel.
                // Process::Exit() will call Scheduler::Wake(this) when it exits.
                ceryx::proc::Scheduler::Block(child);
                // Re-validate: child must be zombie after wakeup.
                if (!child->IsZombie()) return static_cast<u64>(-ECHILD);
            }

            u64 child_pid = child->GetPid();
            if (wstatus) {
                // Linux WEXITSTATUS format: exit code in bits 8..15.
                *wstatus = (child->GetExitStatus() & 0xFF) << 8;
            }

            proc->RemoveChild(child);
            child->Reap(); // Free child's address space + Process object.
            return child_pid;
        }
        case 110: { // sys_getppid
            auto* proc = thread ? thread->GetProcess() : nullptr;
            if (!proc) return 0;
            auto* parent = proc->GetParent();
            return parent ? static_cast<u64>(parent->GetPid()) : 0;
        }
        default: break;
    }

    return static_cast<u64>(-ENOSYS);
}

} // namespace ceryx::cpu
