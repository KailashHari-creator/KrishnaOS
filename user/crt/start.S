.intel_syntax noprefix

.section .text

.extern main
.extern krishna_exit

.global _start
.type _start, @function

_start:
    /*
     * No argc/argv/environment exists yet.
     */
    xor ebp, ebp

    call main

    /*
     * main() returned its exit status in EAX.
     */
    movsxd rdi, eax

    call krishna_exit

    ud2

.size _start, . - _start


.section .note.GNU-stack,"",@progbits