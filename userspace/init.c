typedef unsigned long long size_t;
typedef long long          ssize_t;
typedef int                pid_t;

// Syscall signatures
extern ssize_t write(int fd, const void* buf, size_t count);
extern void*   mmap(void* addr, size_t len, int prot, int flags, int fd, long off);
extern int     munmap(void* addr, size_t len);
extern void*   brk(void* addr);
extern int     pipe(int fds[2]);
extern int     close(int fd);
extern ssize_t read(int fd, void* buf, size_t count);

// Process management
extern pid_t   fork(void);
extern pid_t   getpid(void);
extern pid_t   getppid(void);
extern pid_t   waitpid(pid_t pid, int* wstatus, int options);

struct siginfo_t {
    int si_signo, si_errno, si_code;
    void* si_addr;
};

struct sigaction {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, struct siginfo_t*, void*);
    unsigned long long sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

extern int  rt_sigaction(int sig, const struct sigaction* act, struct sigaction* oact);
extern void sig_restorer();

#define SIGSEGV     11
#define SA_RESTORER 0x04000000

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)

extern void _exit(int status);

void print(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

// Print a decimal integer (non-negative).
void print_int(long long n) {
    if (n < 0) { write(1, "-", 1); n = -n; }
    if (n == 0) { write(1, "0", 1); return; }
    char buf[20];
    int  i = 20;
    while (n > 0) { buf[--i] = '0' + (n % 10); n /= 10; }
    write(1, buf + i, 20 - i);
}

static int sigsegv_caught = 0;
void handle_sigsegv(int sig) {
    (void)sig;
    sigsegv_caught = 1;
    print("[INIT] Caught SIGSEGV! Signal subsystem is working.\n");
    _exit(0);
}

int main() {
    print("[INIT] Starting syscall validation...\n");

    // ── 1. brk ────────────────────────────────────────────────────────────────
    void* initial_brk = brk(0);
    void* new_brk     = brk((char*)initial_brk + 4096);
    if (new_brk > initial_brk) {
        print("[INIT] brk expansion successful. Testing write...\n");
        unsigned int* heap_val = (unsigned int*)initial_brk;
        *heap_val = 0xDEADC0DE;
        if (*heap_val == 0xDEADC0DE)
            print("[INIT] brk write/read verified.\n");
    } else {
        print("[INIT] brk expansion failed!\n");
    }

    // ── 2. mmap / munmap ──────────────────────────────────────────────────────
    print("[INIT] Testing mmap anonymous...\n");
    void* map_addr = mmap(0, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)map_addr > 0) {
        print("[INIT] mmap successful. Testing write...\n");
        *(unsigned int*)map_addr = 0xCAFEBABE;
        if (*(unsigned int*)map_addr == 0xCAFEBABE)
            print("[INIT] mmap write/read verified.\n");
        munmap(map_addr, 4096);
        print("[INIT] munmap completed.\n");
    } else {
        print("[INIT] mmap failed!\n");
    }

    // ── 3. pipe ───────────────────────────────────────────────────────────────
    print("[INIT] Testing pipe...\n");
    int p_fds[2];
    if (pipe(p_fds) == 0) {
        print("[INIT] pipe successful. Testing data transfer...\n");
        const char* msg = "Hello from Pipe!";
        write(p_fds[1], msg, 16);

        char buf[32];
        ssize_t rd = read(p_fds[0], buf, 32);
        if (rd == 16) {
            buf[16] = '\0';
            print("[INIT] Read from pipe: ");
            print(buf);
            print("\n");
        } else {
            print("[INIT] pipe read failed or unexpected size.\n");
        }

        close(p_fds[0]);
        close(p_fds[1]);
        print("[INIT] pipe descriptors closed.\n");
    } else {
        print("[INIT] pipe failed!\n");
    }

    // ── 5. fork / waitpid ─────────────────────────────────────────────────────
    print("[INIT] Testing fork()...\n");

    pid_t my_pid = getpid();
    print("[INIT] Parent PID: ");
    print_int(my_pid);
    print("\n");

    pid_t child = fork();

    if (child < 0) {
        print("[INIT] fork() failed!\n");
    } else if (child == 0) {
        // ── Child process ─────────────────────────────────────────────────────
        pid_t cpid  = getpid();
        pid_t ppid  = getppid();

        print("[CHILD] Hello from child process! PID=");
        print_int(cpid);
        print(" PPID=");
        print_int(ppid);
        print("\n");

        // Verify CoW: the CoW page we read is shared, write must not corrupt parent.
        unsigned int* cow_val = (unsigned int*)initial_brk;
        *cow_val = 0x11223344; // Private CoW write — only affects child's copy.
        print("[CHILD] CoW write to heap succeeded.\n");

        _exit(42); // Exit with status 42.
    } else {
        // ── Parent process ────────────────────────────────────────────────────
        print("[INIT] fork() returned child PID: ");
        print_int(child);
        print("\n");

        int wstatus = 0;
        pid_t waited = waitpid(child, &wstatus, 0);

        if (waited == child) {
            int exited_status = WEXITSTATUS(wstatus);
            print("[INIT] waitpid() collected child ");
            print_int(waited);
            print(", exit status=");
            print_int(exited_status);
            print("\n");

            if (exited_status == 42) {
                print("[INIT] fork/waitpid: PASSED.\n");
            } else {
                print("[INIT] fork/waitpid: FAILED (wrong exit status).\n");
            }
        } else {
            print("[INIT] waitpid() failed!\n");
        }

        // Verify CoW isolation: parent's copy must still be 0xDEADC0DE.
        unsigned int* cow_check = (unsigned int*)initial_brk;
        if (*cow_check == 0xDEADC0DE) {
            print("[INIT] CoW isolation verified: parent's memory is unmodified.\n");
        } else {
            print("[INIT] CoW FAILURE: parent's memory was corrupted!\n");
        }
    }

    // ── 4. Signals (SIGSEGV) ──────────────────────────────────────────────────
    print("[INIT] Testing signals (SIGSEGV)...\n");
    struct sigaction sa;
    sa.sa_handler  = handle_sigsegv;
    sa.sa_sigaction = 0;
    sa.sa_flags    = SA_RESTORER;
    sa.sa_restorer = sig_restorer;
    sa.sa_mask     = 0;

    if (rt_sigaction(SIGSEGV, &sa, 0) == 0) {
        print("[INIT] rt_sigaction successful. Triggering page fault...\n");
        volatile int* p = 0;
        *p = 123; // → SIGSEGV → handle_sigsegv → _exit(0) … should not reach below
        print("[INIT] ERROR: Should not reach here after SIGSEGV!\n");
    } else {
        print("[INIT] rt_sigaction failed!\n");
    }

    print("[INIT] All tests passed. Exiting.\n");
    _exit(0);
    return 0;
}
