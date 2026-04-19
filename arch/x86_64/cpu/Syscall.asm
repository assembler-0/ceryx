[bits 64]
extern SyscallDispatch

global SyscallEntry
SyscallEntry:
    ; SYSCALL:
    ; RCX = return RIP
    ; R11 = RFLAGS
    ; Kernel GS should be swapped in

    swapgs
    mov [gs:24], rsp ; Store user RSP in CpuData.user_rsp
    mov rsp, [gs:16] ; Load kernel stack from CpuData.kernel_stack

    ; Create SyscallRegisters on stack
    ; struct SyscallRegisters {
    ;    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    ;    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    ;    u64 rip, cs, rflags, rsp, ss;
    ; }
    
    push 0x1B        ; ss (User Data: 0x18 | 3)
    push qword [gs:24] ; rsp
    push r11         ; rflags
    push 0x23        ; cs (User Code: 0x20 | 3)
    push rcx         ; rip
    
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    cld
    call SyscallDispatch
    
    ; rax contains return value
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    ; Don't pop rax yet, it's our return value

    ; Restore state for SYSRET
    mov r11, [rsp + 16] ; rflags
    mov rcx, [rsp + 0]  ; rip
    
    mov rsp, [gs:24] ; Restore user RSP
    swapgs
    
    o64 sysret
