bits 64

section .text

extern syscall_interrupt_dispatch

global arch_syscall_interrupt_entry


%macro PUSH_GENERAL_REGISTERS 0
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro


%macro POP_GENERAL_REGISTERS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro


;
; A Ring-3 INT instruction causes the processor to:
;
; 1. Load RSP from TSS.RSP0.
; 2. Push user SS, RSP, RFLAGS, CS and RIP.
; 3. Enter this routine in Ring 0.
;
arch_syscall_interrupt_entry:
    cld

    PUSH_GENERAL_REGISTERS

    mov rdi, rsp
    and rsp, -16

    call syscall_interrupt_dispatch

    ;
    ; The dispatcher returns the context to restore.
    ; SYS_EXIT never returns from the dispatcher.
    ;
    mov rsp, rax

    POP_GENERAL_REGISTERS
    iretq


section .note.GNU-stack noalloc noexec nowrite progbits