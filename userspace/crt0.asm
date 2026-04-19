[bits 64]

section .text
global _start
extern main
extern _exit

_start:
    ; Set up a minimal stack frame
    xor rbp, rbp
    
    ; Call main
    call main
    
    ; Exit with main's return value
    mov rdi, rax
    call _exit

    ; Safety loop
.loop:
    hlt
    jmp .loop
