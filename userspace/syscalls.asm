[bits 64]

section .text

global write
write:
    mov rax, 1      ; sys_write
    syscall
    ret

global _exit
_exit:
    mov rax, 60     ; sys_exit
    syscall
    ret

global read
read:
    mov rax, 0      ; sys_read
    syscall
    ret

global close
close:
    mov rax, 3      ; sys_close
    syscall
    ret

global mmap
mmap:
    mov r10, rcx    ; 4th arg is r10 for syscall, rcx for C
    mov rax, 9      ; sys_mmap
    syscall
    ret

global munmap
munmap:
    mov rax, 11     ; sys_munmap
    syscall
    ret

global brk
brk:
    mov rax, 12     ; sys_brk
    syscall
    ret

global execve
execve:
    mov rax, 59     ; sys_execve
    syscall
    ret

global pipe
pipe:
    mov rax, 22     ; sys_pipe
    syscall
    ret

global rt_sigaction
rt_sigaction:
    mov rax, 13     ; sys_rt_sigaction
    syscall
    ret

global sig_restorer
sig_restorer:
    mov rax, 15     ; sys_rt_sigreturn
    syscall
    ret

global fork
fork:
    mov rax, 57     ; sys_fork
    syscall
    ret

global getpid
getpid:
    mov rax, 39     ; sys_getpid
    syscall
    ret

global getppid
getppid:
    mov rax, 110    ; sys_getppid
    syscall
    ret

global waitpid
waitpid:
    ; rdi=pid, rsi=*wstatus, rdx=options  (already in correct regs)
    mov rax, 61     ; sys_waitpid
    syscall
    ret
