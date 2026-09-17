bits 64

section .text

extern local_apic_timer_interrupt_dispatch
extern kernel_thread_reschedule_interrupt

global arch_local_apic_timer_interrupt_entry
global arch_reschedule_interrupt_entry
global arch_request_context_switch

%define ARCH_RESCHEDULE_VECTOR 0xF1


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



;  * Hardware has already pushed:
;  *
;  *     RIP, CS, RFLAGS
;  *
;  * Save every GPR, ask C which complete context should run,
;  * switch RSP to that context, and return through IRETQ.

arch_local_apic_timer_interrupt_entry:
    cld
    PUSH_GENERAL_REGISTERS

    mov rdi, rsp

    ; /*
    ;  * Align the temporary C-call stack. The complete interrupt frame
    ;  * remains reachable through RDI.
    ;  */
    and rsp, -16

    call local_apic_timer_interrupt_dispatch

    ; /*
    ;  * RAX is the selected thread's saved RSP.
    ;  */
    mov rsp, rax

    POP_GENERAL_REGISTERS
    iretq


arch_reschedule_interrupt_entry:
    cld
    PUSH_GENERAL_REGISTERS

    mov rdi, rsp
    and rsp, -16

    call kernel_thread_reschedule_interrupt

    mov rsp, rax

    POP_GENERAL_REGISTERS
    iretq


; /*
;  * Called as a normal System V function.
;  *
;  * When this thread is selected again, IRETQ returns after INT and
;  * RET returns to kernel_thread_yield(), block(), or the caller.
;  */
arch_request_context_switch:
    int ARCH_RESCHEDULE_VECTOR
    ret


section .note.GNU-stack noalloc noexec nowrite progbits