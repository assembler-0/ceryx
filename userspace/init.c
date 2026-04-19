
typedef unsigned long long size_t;

// Minimal syscall wrappers
extern long write(int fd, const void* buf, size_t count);
extern void _exit(int status);

int main() {
    const char* msg = "[INIT] Hello from Ceryx userspace!\n";
    // We can't use sizeof(msg) here now, so we'll just use a fixed length or a strlen (if we had it).
    // "[INIT] Hello from Ceryx userspace!\n" is 35 chars.
    write(1, msg, 35);
    
    _exit(0);

    
    // Should not reach here
    for(;;);
    return 0;
}
