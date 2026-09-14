section .text

global arch_context_switch

;
; System V AMD64:
;
;     RDI = address at which to save the old RSP
;     RSI = new RSP
;
; Save every callee-saved general-purpose register. The return address
; was already placed on the stack by CALL.
;
arch_context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret


section .note.GNU-stack noalloc noexec nowrite progbits