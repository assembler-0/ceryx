[bits 64]

global SwitchContext
global ThreadTrampoline
global UserspaceTrampoline
global ForkReturn
extern ThreadDestroy

SwitchContext:
    ; rdi = prev_thread_stack_ptr (address of m_regs.rsp)
    ; rsi = next_thread_stack (the new rsp value)

    ; Save current state (only non-volatile registers)
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Save the current stack pointer into prev thread structure
    mov [rdi], rsp

    ; Load the next thread's stack pointer
    mov rsp, rsi

    ; Restore registers for the next thread
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret

; This is the landing point for all new threads.
; When a thread is created, its stack is set up so that SwitchContext 'returns' here.
; R12 = entry_point
; R13 = arg
; R14 = thread_ptr (for cleanup)
ThreadTrampoline:
    ; We are now in the new thread's context.
    ; SwitchContext has already popped the dummy registers.

    sti ; New threads should start with interrupts enabled.

    mov rdi, r13 ; arg
    call r12     ; call entry_point(arg)

    ; If the thread returns, we need to clean it up.
    mov rdi, r14 ; thread_ptr
    call ThreadDestroy

    ; ThreadDestroy should not return.
.halt:
    cli
    hlt
    jmp .halt

; This is the landing point for jumping into Ring 3.
; R12 = user_rip
; R13 = user_rsp
UserspaceTrampoline:
    ; We are currently in Ring 0. GS points to kernel data.
    ; We swap GS so that the kernel data is stored in KERNEL_GS_BASE
    ; and the user can have their own GS base (currently uninitialized/zero).
    swapgs

    cli
    ; We use IRETQ to drop into Ring 3.
    ; Stack for IRETQ: SS, RSP, RFLAGS, CS, RIP

    push 0x1B ; User Data Segment (RPL 3)
    push r13  ; User RSP
    push 0x202; RFLAGS (Interrupts enabled, bit 1 set)
    push 0x23 ; User Code Segment (RPL 3)
    push r12  ; User RIP

    ; Clear general purpose registers for safety
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    ; r12-r15 were used for entry parameters and are now irrelevant to the user program.

    iretq

; ──────────────────────────────────────────────────────────────────────────────
; ForkReturn — landing point for a newly forked child thread.
;
; InitializeForkStack() builds the child kernel stack as:
;   [switch frame: r15..rbp + ret→ForkReturn]  ← SwitchContext pops these
;   [InterruptFrame: r15..rax=0, dummy×2, rip, cs, rflags, rsp_user, ss]
;
; After SwitchContext pops its 6 regs and 'ret's here, rsp points to
; InterruptFrame.r15. We restore all GP regs and iretq to userspace.
; The child gets rax=0 (fork() returns 0 in child).
; ──────────────────────────────────────────────────────────────────────────────
ForkReturn:
    swapgs              ; restore user GS before Ring 3

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
    pop rax             ; rax = 0  (fork returns 0 in child)

    add rsp, 16         ; skip dummy interrupt_number + error_code

    iretq               ; → rip, cs, rflags, user rsp, ss
