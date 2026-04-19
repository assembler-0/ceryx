#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <ceryx/cpu/Idt.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl;

class Thread;

using sigset_t = u64;

class Signal {
public:
    static void DoPendingSignals(cpu::InterruptFrame* frame);
    static void Send(Thread* thread, int sig);
    static void Raise(int sig);
};

#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1    10
#define SIGSEGV    11
#define SIGUSR2    12
#define SIGPIPE    13
#define SIGALRM    14
#define SIGTERM    15
#define SIGSTKFLT  16
#define SIGCHLD    17
#define SIGCONT    18
#define SIGSTOP    19
#define SIGTSTP    20
#define SIGTTIN    21
#define SIGTTOU    22
#define SIGURG     23
#define SIGXCPU    24
#define SIGXFSZ    25
#define SIGVTALRM  26
#define SIGPROF    27
#define SIGWINCH   28
#define SIGIO      29
#define SIGPWR     30
#define SIGSYS     31
#define NSIG       64

// sigaction flags
#define SA_NOCLDSTOP 0x00000001u
#define SA_NOCLDWAIT 0x00000002u
#define SA_SIGINFO   0x00000004u
#define SA_ONSTACK   0x08000000u
#define SA_RESTART   0x10000000u
#define SA_NODEFER   0x40000000u
#define SA_RESETHAND 0x80000000u
#define SA_RESTORER  0x04000000u

// Special handlers
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

struct siginfo_t {
    int si_signo;
    int si_errno;
    int si_code;
    void* si_addr;
};

struct sigaction {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t*, void*);
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

// ucontext for x86_64
struct mcontext_t {
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
    u64 rdi, rsi, rbp, rbx, rdx, rax, rcx;
    u64 rsp, rip, eflags;
    u64 cs, gs, fs, pad0;
    u64 err, trapno;
    u64 oldmask, cr2;
};

struct ucontext_t {
    u64 uc_flags;
    struct ucontext_t* uc_link;
    void* uc_stack_ptr;
    usize uc_stack_size;
    mcontext_t uc_mcontext;
    sigset_t uc_sigmask;
};

struct sigframe {
    ucontext_t uc;
    siginfo_t info;
};

} // namespace ceryx::proc
