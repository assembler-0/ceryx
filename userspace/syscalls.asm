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
