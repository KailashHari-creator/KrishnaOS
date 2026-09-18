bits 64

section .text

global arch_gdt_load

;
; System V arguments:
;
; RDI = pointer to GDTR structure
; RSI = kernel code selector
; RDX = kernel data selector
; RCX = TSS selector
;
arch_gdt_load:
    cli

    lgdt [rdi]

    ;
    ; Reload the ordinary data-segment registers.
    ;
    mov ax, dx

    mov ds, ax
    mov es, ax
    mov ss, ax

    ;
    ; FS and GS will later be used for per-CPU and per-thread data.
    ; Keep their selectors null for now.
    ;
    xor eax, eax
    mov fs, ax
    mov gs, ax

    ;
    ; CS cannot be loaded with MOV. A far return reloads CS and
    ; continues execution at .reload_cs.
    ;
    movzx rax, si
    push rax

    lea rax, [rel .reload_cs]
    push rax

    retfq

.reload_cs:
    ;
    ; Load the task register with KRISHNA's 64-bit TSS.
    ;
    mov ax, cx
    ltr ax

    ret


section .note.GNU-stack noalloc noexec nowrite progbits