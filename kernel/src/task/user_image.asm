bits 64

section .rodata align=16

global embedded_user_test_elf_start
global embedded_user_test_elf_end

embedded_user_test_elf_start:
    incbin "../user/bin/user_test.elf"

embedded_user_test_elf_end:


section .note.GNU-stack noalloc noexec nowrite progbits