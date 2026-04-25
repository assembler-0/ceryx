#include <ceryx/cpu/Syscall.hpp>
#include <ceryx/cpu/Gdt.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <drivers/debugcon.hpp>
#include <ceryx/proc/Scheduler.hpp>
#include <ceryx/proc/Process.hpp>
#include <ceryx/proc/ElfLoader.hpp>
#include <ceryx/proc/Reaper.hpp>
#include <ceryx/proc/Signal.hpp>
#include <ceryx/fs/FileDescription.hpp>
#include <ceryx/fs/Vfs.hpp>
#include <ceryx/fs/pipe/PipeVnode.hpp>
#include <ceryx/fs/Stat.hpp>
#include <FoundationKitPlatform/Clocksource/TimeKeeper.hpp>
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

            FK_LOG_INFO("sys_waitpid: caller=PID{} wpid={} search_pid={} child={} children_size={}",
                proc->GetPid(), wpid, search_pid,
                child ? child->GetPid() : 0ULL,
                proc->ChildCount());

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
        case 2: { // sys_open
            if (!process) return static_cast<u64>(-ENOSYS);
            const char* user_path = reinterpret_cast<const char*>(regs->rdi);
            int flags = static_cast<int>(regs->rsi);
            // mode = regs->rdx (ignored for now, no permission enforcement)

            // Resolve the vnode, creating it if O_CREAT is set and it doesn't exist.
            RefPtr<fs::Vnode> target_vnode;
            {
                auto vnode_res = fs::Vfs::PathToVnode(process->GetRoot(), user_path);
                if (vnode_res.HasValue()) {
                    // File exists — handle O_TRUNC.
                    if (flags & 0x200) vnode_res.Value()->size = 0; // O_TRUNC
                    target_vnode = vnode_res.Value();
                } else if (flags & 0x40) { // O_CREAT
                    // Decompose path into parent dir + filename.
                    StringView path_sv(user_path);
                    usize last_slash = path_sv.RFind('/');
                    RefPtr<fs::Vnode> parent_dir;
                    StringView filename;
                    if (last_slash == StringView::NPos || last_slash == 0) {
                        parent_dir = process->GetCwd();
                        filename   = path_sv;
                    } else {
                        auto parent_res = fs::Vfs::PathToVnode(
                            process->GetRoot(),
                            path_sv.SubView(0, last_slash == 0 ? 1 : last_slash));
                        if (!parent_res) return static_cast<u64>(parent_res.Error());
                        parent_dir = parent_res.Value();
                        filename   = path_sv.SubView(last_slash + 1);
                    }
                    if (!parent_dir || !parent_dir->ops || !parent_dir->ops->Create)
                        return static_cast<u64>(-ENOENT);
                    auto create_res = parent_dir->ops->Create(*parent_dir, filename);
                    if (!create_res) return static_cast<u64>(create_res.Error());
                    target_vnode = create_res.Value();
                } else {
                    return static_cast<u64>(vnode_res.Error());
                }
            }

            auto desc = RefPtr<fs::FileDescription>(
                new fs::FileDescription(target_vnode, static_cast<u32>(flags)));
            // O_APPEND: position at end of file.
            if (flags & 0x400) desc->offset = target_vnode->size;

            auto fd_res = process->AllocateFd(desc);
            if (!fd_res) return static_cast<u64>(fd_res.Error());
            return fd_res.Value();
        }
        case 257: { // sys_openat
            if (!process) return static_cast<u64>(-ENOSYS);
            // dirfd = regs->rdi (AT_FDCWD = -100, we only support that for now)
            // Reuse open logic: forward path/flags into rdi/rsi and re-dispatch as open (case 2)
            const char* user_path = reinterpret_cast<const char*>(regs->rsi);
            int flags = static_cast<int>(regs->rdx);
            regs->rdi = reinterpret_cast<u64>(user_path);
            regs->rsi = static_cast<u64>(static_cast<u32>(flags));
            regs->rax = 2;
            return Syscall::Dispatch(regs); // tail-recurse into open
        }
        case 8: { // sys_lseek
            if (!process) return static_cast<u64>(-ENOSYS);
            auto fd_desc = process->GetFd(static_cast<u32>(regs->rdi));
            if (!fd_desc) return static_cast<u64>(-EBADF);
            i64 offset = static_cast<i64>(regs->rsi);
            int whence = static_cast<int>(regs->rdx);
            switch (whence) {
                case 0: // SEEK_SET
                    if (offset < 0) return static_cast<u64>(-EINVAL);
                    fd_desc->offset = static_cast<usize>(offset);
                    break;
                case 1: // SEEK_CUR
                    if (offset < 0 && static_cast<usize>(-offset) > fd_desc->offset)
                        return static_cast<u64>(-EINVAL);
                    fd_desc->offset = static_cast<usize>(static_cast<i64>(fd_desc->offset) + offset);
                    break;
                case 2: // SEEK_END
                    if (!fd_desc->vnode) return static_cast<u64>(-EBADF);
                    fd_desc->offset = static_cast<usize>(static_cast<i64>(fd_desc->vnode->size) + offset);
                    break;
                default:
                    return static_cast<u64>(-EINVAL);
            }
            return fd_desc->offset;
        }
        case 32: { // sys_dup
            if (!process) return static_cast<u64>(-ENOSYS);
            auto fd_desc = process->GetFd(static_cast<u32>(regs->rdi));
            if (!fd_desc) return static_cast<u64>(-EBADF);
            auto new_fd = process->AllocateFd(fd_desc);
            if (!new_fd) return static_cast<u64>(new_fd.Error());
            return new_fd.Value();
        }
        case 33: { // sys_dup2
            if (!process) return static_cast<u64>(-ENOSYS);
            u32 old_fd = static_cast<u32>(regs->rdi);
            u32 new_fd_num = static_cast<u32>(regs->rsi);
            if (old_fd == new_fd_num) return new_fd_num;
            auto fd_desc = process->GetFd(old_fd);
            if (!fd_desc) return static_cast<u64>(-EBADF);
            auto res = process->AllocateFdAt(new_fd_num, fd_desc);
            if (!res) return static_cast<u64>(res.Error());
            return res.Value();
        }
        case 4: { // sys_stat
            if (!process) return static_cast<u64>(-ENOSYS);
            const char* user_path = reinterpret_cast<const char*>(regs->rdi);
            fs::KernelStat* user_stat = reinterpret_cast<fs::KernelStat*>(regs->rsi);
            auto vnode_res = fs::Vfs::PathToVnode(process->GetRoot(), user_path);
            if (!vnode_res) return static_cast<u64>(vnode_res.Error());
            fs::KernelStat kstat{};
            fs::VnodeToStat(*vnode_res.Value(), kstat);
            *user_stat = kstat;
            return 0;
        }
        case 5: { // sys_fstat
            if (!process) return static_cast<u64>(-ENOSYS);
            auto fd_desc = process->GetFd(static_cast<u32>(regs->rdi));
            if (!fd_desc) return static_cast<u64>(-EBADF);
            fs::KernelStat* user_stat = reinterpret_cast<fs::KernelStat*>(regs->rsi);
            if (!fd_desc->vnode) return static_cast<u64>(-EBADF);
            fs::KernelStat kstat{};
            fs::VnodeToStat(*fd_desc->vnode, kstat);
            *user_stat = kstat;
            return 0;
        }
        case 6: { // sys_lstat (same as stat for now — no symlink resolution)
            if (!process) return static_cast<u64>(-ENOSYS);
            const char* user_path = reinterpret_cast<const char*>(regs->rdi);
            fs::KernelStat* user_stat = reinterpret_cast<fs::KernelStat*>(regs->rsi);
            auto vnode_res = fs::Vfs::PathToVnode(process->GetRoot(), user_path);
            if (!vnode_res) return static_cast<u64>(vnode_res.Error());
            fs::KernelStat kstat{};
            fs::VnodeToStat(*vnode_res.Value(), kstat);
            *user_stat = kstat;
            return 0;
        }
        case 262: { // sys_newfstatat
            if (!process) return static_cast<u64>(-ENOSYS);
            // dirfd = rdi (AT_FDCWD=-100 only supported), path = rsi, statbuf = rdx
            const char* user_path = reinterpret_cast<const char*>(regs->rsi);
            fs::KernelStat* user_stat = reinterpret_cast<fs::KernelStat*>(regs->rdx);
            auto vnode_res = fs::Vfs::PathToVnode(process->GetRoot(), user_path);
            if (!vnode_res) return static_cast<u64>(vnode_res.Error());
            fs::KernelStat kstat{};
            fs::VnodeToStat(*vnode_res.Value(), kstat);
            *user_stat = kstat;
            return 0;
        }
        case 228: { // sys_clock_gettime
            struct KTimespec { i64 tv_sec; i64 tv_nsec; };
            int clk_id = static_cast<int>(regs->rdi);
            KTimespec* tp = reinterpret_cast<KTimespec*>(regs->rsi);
            if (!tp) return static_cast<u64>(-EFAULT);
            u64 ns = 0;
            if (clk_id == 0) { // CLOCK_REALTIME
                ns = static_cast<u64>(FoundationKitPlatform::Clocksource::TimeKeeper::NowWallClock());
            } else { // CLOCK_MONOTONIC and everything else
                ns = FoundationKitPlatform::Clocksource::TimeKeeper::NowNanoseconds();
            }
            tp->tv_sec  = static_cast<i64>(ns / 1000000000ULL);
            tp->tv_nsec = static_cast<i64>(ns % 1000000000ULL);
            return 0;
        }
        case 96: { // sys_gettimeofday
            struct KTimeval { i64 tv_sec; i64 tv_usec; };
            KTimeval* tv = reinterpret_cast<KTimeval*>(regs->rdi);
            if (tv) {
                u64 ns = FoundationKitPlatform::Clocksource::TimeKeeper::NowNanoseconds();
                tv->tv_sec  = static_cast<i64>(ns / 1000000000ULL);
                tv->tv_usec = static_cast<i64>((ns % 1000000000ULL) / 1000ULL);
            }
            return 0;
        }
        case 35: { // sys_nanosleep
            struct KTimespec { i64 tv_sec; i64 tv_nsec; };
            const KTimespec* req = reinterpret_cast<const KTimespec*>(regs->rdi);
            if (!req) return static_cast<u64>(-EFAULT);
            u64 sleep_ns = static_cast<u64>(req->tv_sec) * 1000000000ULL
                         + static_cast<u64>(req->tv_nsec);
            u64 deadline = FoundationKitPlatform::Clocksource::TimeKeeper::NowNanoseconds() + sleep_ns;
            // Spin-wait using the TimeKeeper — a proper timer wheel will replace this
            // once the timer subsystem is wired. For now this is correct for short sleeps.
            while (FoundationKitPlatform::Clocksource::TimeKeeper::NowNanoseconds() < deadline) {
                ceryx::proc::Scheduler::Yield();
            }
            return 0;
        }
        case 56: { // sys_clone (minimal: CLONE_VM|CLONE_FILES|CLONE_SIGHAND = new thread)
            if (!process || !thread) return static_cast<u64>(-ENOSYS);
            u64 clone_flags = regs->rdi;
            u64 user_stack  = regs->rsi;
            constexpr u64 CLONE_VM     = 0x00000100ULL;
            if (!(clone_flags & CLONE_VM)) {
                // fork-like clone: delegate to Process::Fork
                auto child_res = process->Fork(regs);
                if (!child_res.HasValue()) return static_cast<u64>(child_res.Error());
                return static_cast<u64>(child_res.Value()->GetPid());
            }
            // Thread clone: same address space, new kernel thread
            auto* new_thread = new ceryx::proc::Thread(process, false);
            // rdx holds the entry function for the new thread (clone calling convention)
            new_thread->InitializeUserStack(regs->rdx, user_stack);
            new_thread->SetName("clone_thread");
            ceryx::proc::Scheduler::AddThread(new_thread, 0);
            return static_cast<u64>(new_thread->GetId());
        }
        case 78: { // sys_gettid
            return thread ? static_cast<u64>(thread->GetId()) : 0;
        }
        case 102: { // sys_getuid
            return 0; // root for now
        }
        case 104: { // sys_getgid
            return 0;
        }
        case 107: { // sys_geteuid
            return 0;
        }
        case 108: { // sys_getegid
            return 0;
        }
        case 158: { // sys_arch_prctl
            constexpr int ARCH_SET_FS = 0x1002;
            constexpr int ARCH_GET_FS = 0x1003;
            constexpr int ARCH_SET_GS = 0x1001;
            constexpr u32 kMsrFsBase  = 0xC0000100u;
            constexpr u32 kMsrGsBase  = 0xC0000101u;
            int code = static_cast<int>(regs->rdi);
            u64 addr = regs->rsi;
            switch (code) {
                case ARCH_SET_FS:
                    ControlRegs::WriteMsr(kMsrFsBase, addr);
                    return 0;
                case ARCH_GET_FS: {
                    u64 val = ControlRegs::ReadMsr(kMsrFsBase);
                    *reinterpret_cast<u64*>(addr) = val;
                    return 0;
                }
                case ARCH_SET_GS:
                    ControlRegs::WriteMsr(kMsrGsBase, addr);
                    return 0;
                default:
                    return static_cast<u64>(-EINVAL);
            }
        }
        case 231: { // sys_exit_group (same as exit for single-threaded)
            int status = static_cast<int>(regs->rdi);
            auto* proc = thread ? thread->GetProcess() : nullptr;
            if (proc) proc->Exit(status);
            if (thread) {
                thread->SetState(ceryx::proc::ThreadState::Terminated);
                ceryx::proc::Reaper::Enqueue(thread);
                ceryx::proc::Scheduler::Yield(); // Never returns.
            }
            return 0;
        }
        case 63: { // sys_uname
            struct KUtsname {
                char sysname[65];
                char nodename[65];
                char release[65];
                char version[65];
                char machine[65];
                char domainname[65];
            };
            KUtsname* uts = reinterpret_cast<KUtsname*>(regs->rdi);
            if (!uts) return static_cast<u64>(-EFAULT);
            // Copy strings manually (no libc)
            auto copy_str = [](char* dst, const char* src, usize max) {
                usize i = 0;
                while (i < max - 1 && src[i]) { dst[i] = src[i]; ++i; }
                dst[i] = '\0';
            };
            copy_str(uts->sysname,    "Linux",            65);
            copy_str(uts->nodename,   "ceryx",            65);
            copy_str(uts->release,    "6.1.0",            65);
            copy_str(uts->version,    "#1 ceryx-kernel",  65);
            copy_str(uts->machine,    "x86_64",           65);
            copy_str(uts->domainname, "(none)",            65);
            return 0;
        }
        case 217: { // sys_getdents64
            if (!process) return static_cast<u64>(-ENOSYS);
            auto fd_desc = process->GetFd(static_cast<u32>(regs->rdi));
            if (!fd_desc) return static_cast<u64>(-EBADF);
            if (!fd_desc->vnode || fd_desc->vnode->type != fs::VnodeType::Directory)
                return static_cast<u64>(-ENOTDIR);
            if (!fd_desc->vnode->ops || !fd_desc->vnode->ops->Iterate)
                return 0; // No iterate op — empty directory.
            void* buf     = reinterpret_cast<void*>(regs->rsi);
            usize buf_len = static_cast<usize>(regs->rdx);
            // fd_desc->offset is the directory cookie (child index).
            auto res = fd_desc->vnode->ops->Iterate(
                *fd_desc->vnode, buf, buf_len, fd_desc->offset);
            if (!res) return static_cast<u64>(res.Error());
            // Advance the cookie by the number of entries consumed.
            // The Iterate op sets d_off = next cookie in each record;
            // we advance fd_desc->offset to the last d_off written.
            if (res.Value() > 0) {
                // Walk the written records to find the last d_off.
                struct D64Hdr { u64 d_ino; i64 d_off; u16 d_reclen; u8 d_type; };
                u8* p   = static_cast<u8*>(buf);
                u8* end = p + res.Value();
                while (p < end) {
                    auto* hdr = reinterpret_cast<D64Hdr*>(p);
                    fd_desc->offset = static_cast<usize>(hdr->d_off);
                    p += hdr->d_reclen;
                }
            }
            return res.Value();
        }
        case 10: { // sys_mprotect
            if (!process) return static_cast<u64>(-ENOSYS);
            u64 addr  = regs->rdi;
            u64 len   = regs->rsi;
            u64 prot  = regs->rdx;
            if (len == 0 || (addr & 0xFFF)) return static_cast<u64>(-EINVAL);
            u64 aligned_len = (len + 0xFFF) & ~0xFFFULL;

            FoundationKitMemory::VmaProt vma_prot = FoundationKitMemory::VmaProt::User;
            if (prot & 0x1) vma_prot = vma_prot | FoundationKitMemory::VmaProt::Read;
            if (prot & 0x2) vma_prot = vma_prot | FoundationKitMemory::VmaProt::Write;
            if (prot & 0x4) vma_prot = vma_prot | FoundationKitMemory::VmaProt::Execute;

            auto res = process->GetAddressSpace().GetInner().Protect(
                FoundationKitMemory::VirtualAddress{addr}, aligned_len, vma_prot);
            if (!res) return static_cast<u64>(-EINVAL);
            return 0;
        }
        case 72: { // sys_fcntl
            if (!process) return static_cast<u64>(-ENOSYS);
            u32 fd  = static_cast<u32>(regs->rdi);
            int cmd = static_cast<int>(regs->rsi);
            u64 arg = regs->rdx;

            constexpr int F_DUPFD       = 0;
            constexpr int F_GETFD       = 1;
            constexpr int F_SETFD       = 2;
            constexpr int F_GETFL       = 3;
            constexpr int F_SETFL       = 4;
            constexpr int F_DUPFD_CLOEXEC = 1030;

            auto fd_desc = process->GetFd(fd);
            if (!fd_desc) return static_cast<u64>(-EBADF);

            switch (cmd) {
                case F_DUPFD:
                case F_DUPFD_CLOEXEC: {
                    // Allocate the lowest available fd >= arg.
                    for (u32 new_fd = static_cast<u32>(arg);
                         new_fd < proc::Process::kMaxFds; ++new_fd) {
                        if (!process->GetFd(new_fd)) {
                            auto res = process->AllocateFdAt(new_fd, fd_desc);
                            if (res) return res.Value();
                        }
                    }
                    return static_cast<u64>(-EMFILE);
                }
                case F_GETFD:
                    // FD_CLOEXEC flag — not tracked yet, return 0.
                    return 0;
                case F_SETFD:
                    // FD_CLOEXEC — accept but ignore for now.
                    return 0;
                case F_GETFL:
                    return fd_desc->flags;
                case F_SETFL:
                    // Allow setting O_NONBLOCK / O_APPEND; mask off access mode bits.
                    fd_desc->flags = (fd_desc->flags & 0x3u) | (static_cast<u32>(arg) & ~0x3u);
                    return 0;
                default:
                    return static_cast<u64>(-EINVAL);
            }
        }
        case 16: { // sys_ioctl
            if (!process) return static_cast<u64>(-ENOSYS);
            auto fd_desc = process->GetFd(static_cast<u32>(regs->rdi));
            if (!fd_desc) return static_cast<u64>(-EBADF);
            u64 request = regs->rsi;

            // TIOCGWINSZ — terminal window size (used by many programs at startup).
            constexpr u64 TIOCGWINSZ = 0x5413;
            if (request == TIOCGWINSZ) {
                struct WinSize { u16 ws_row; u16 ws_col; u16 ws_xpixel; u16 ws_ypixel; };
                auto* ws = reinterpret_cast<WinSize*>(regs->rdx);
                if (ws) {
                    ws->ws_row    = 25;
                    ws->ws_col    = 80;
                    ws->ws_xpixel = 0;
                    ws->ws_ypixel = 0;
                }
                return 0;
            }

            // TCGETS / TCSETS — terminal attributes (used by isatty, tcgetattr).
            constexpr u64 TCGETS = 0x5401;
            constexpr u64 TCSETS = 0x5402;
            if (request == TCGETS || request == TCSETS) return 0;

            // FIONREAD — bytes available to read.
            constexpr u64 FIONREAD = 0x541B;
            if (request == FIONREAD) {
                int* avail = reinterpret_cast<int*>(regs->rdx);
                if (avail) *avail = 0;
                return 0;
            }

            return static_cast<u64>(-ENOTTY);
        }
        case 21: { // sys_access
            if (!process) return static_cast<u64>(-ENOSYS);
            const char* user_path = reinterpret_cast<const char*>(regs->rdi);
            // mode = regs->rsi (F_OK=0, R_OK=4, W_OK=2, X_OK=1) — we allow all for root.
            auto vnode_res = fs::Vfs::PathToVnode(process->GetRoot(), user_path);
            if (!vnode_res) return static_cast<u64>(vnode_res.Error());
            return 0; // root can access everything
        }
        case 89: { // sys_readlink — no symlinks yet, always EINVAL
            return static_cast<u64>(-EINVAL);
        }
        case 41: { // sys_socket — stub returning ENOSYS for now
            return static_cast<u64>(-ENOSYS);
        }
        case 42: { // sys_connect — stub
            return static_cast<u64>(-ENOSYS);
        }
        case 43: { // sys_accept — stub
            return static_cast<u64>(-ENOSYS);
        }
        case 44: { // sys_sendto — stub
            return static_cast<u64>(-ENOSYS);
        }
        case 45: { // sys_recvfrom — stub
            return static_cast<u64>(-ENOSYS);
        }
        case 49: { // sys_bind — stub
            return static_cast<u64>(-ENOSYS);
        }
        case 50: { // sys_listen — stub
            return static_cast<u64>(-ENOSYS);
        }
        case 54: { // sys_setsockopt — stub
            return static_cast<u64>(-ENOSYS);
        }
        case 55: { // sys_getsockopt — stub
            return static_cast<u64>(-ENOSYS);
        }
        case 200: { // sys_tkill — send signal to thread
            if (!process) return static_cast<u64>(-ENOSYS);
            int sig = static_cast<int>(regs->rsi);
            if (sig < 1 || sig >= NSIG) return static_cast<u64>(-EINVAL);
            proc::Signal::Send(thread, sig);
            return 0;
        }
        case 234: { // sys_tgkill — send signal to thread in process group
            if (!process) return static_cast<u64>(-ENOSYS);
            int sig = static_cast<int>(regs->rdx);
            if (sig < 1 || sig >= NSIG) return static_cast<u64>(-EINVAL);
            proc::Signal::Send(thread, sig);
            return 0;
        }
        case 186: { // sys_gettid (duplicate of 78, some programs use this number)
            return thread ? static_cast<u64>(thread->GetId()) : 0;
        }
        case 20: { // sys_writev
            if (!process) return static_cast<u64>(-ENOSYS);
            auto fd_desc = process->GetFd(static_cast<u32>(regs->rdi));
            if (!fd_desc) return static_cast<u64>(-EBADF);
            struct Iovec { u64 iov_base; u64 iov_len; };
            const Iovec* iov = reinterpret_cast<const Iovec*>(regs->rsi);
            int iovcnt = static_cast<int>(regs->rdx);
            if (!iov || iovcnt <= 0) return static_cast<u64>(-EINVAL);
            usize total = 0;
            for (int k = 0; k < iovcnt; ++k) {
                if (iov[k].iov_len == 0) continue;
                auto res = fd_desc->Write(
                    reinterpret_cast<const void*>(iov[k].iov_base), iov[k].iov_len);
                if (!res) return static_cast<u64>(res.Error());
                total += res.Value();
            }
            return total;
        }
        case 19: { // sys_readv
            if (!process) return static_cast<u64>(-ENOSYS);
            auto fd_desc = process->GetFd(static_cast<u32>(regs->rdi));
            if (!fd_desc) return static_cast<u64>(-EBADF);
            struct Iovec { u64 iov_base; u64 iov_len; };
            const Iovec* iov = reinterpret_cast<const Iovec*>(regs->rsi);
            int iovcnt = static_cast<int>(regs->rdx);
            if (!iov || iovcnt <= 0) return static_cast<u64>(-EINVAL);
            usize total = 0;
            for (int k = 0; k < iovcnt; ++k) {
                if (iov[k].iov_len == 0) continue;
                auto res = fd_desc->Read(
                    reinterpret_cast<void*>(iov[k].iov_base), iov[k].iov_len);
                if (!res) return static_cast<u64>(res.Error());
                total += res.Value();
                if (res.Value() < iov[k].iov_len) break; // short read
            }
            return total;
        }
        default: break;
    }

    return static_cast<u64>(-ENOSYS);
}

} // namespace ceryx::cpu
