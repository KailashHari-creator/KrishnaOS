OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /*
     * First userspace executable address.
     */
    . = 0x0000000000400000;

    .text : ALIGN(0x1000)
    {
        *(.text*)
    } :text

    . = ALIGN(0x1000);

    .rodata :
    {
        *(.rodata*)
    } :text

    . = ALIGN(0x1000);

    .data :
    {
        *(.data*)
    } :data

    .bss :
    {
        *(.bss*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}