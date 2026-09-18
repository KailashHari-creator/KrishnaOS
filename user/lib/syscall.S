.intel_syntax noprefix

.section .text

.global krishna_syscall6
.type krishna_syscall6, @function

/*
 * System V function arguments:
 *
 * RDI = syscall number
 * RSI = argument 1
 * RDX = argument 2
 * RCX = argument 3
 * R8  = argument 4
 * R9  = argument 5
 * [RSP + 8] = argument 6
 *
 * KRISHNA syscall ABI:
 *
 * RAX = syscall number
 * RDI = argument 1
 * RSI = argument 2
 * RDX = argument 3
 * R10 = argument 4
 * R8  = argument 5
 * R9  = argument 6
 */
krishna_syscall6:
    mov rax, rdi

    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx

    mov r10, r8
    mov r8, r9

    mov r9, qword ptr [rsp + 8]

    int 0x80

    ret

.size krishna_syscall6, . - krishna_syscall6


.section .note.GNU-stack,"",@progbits