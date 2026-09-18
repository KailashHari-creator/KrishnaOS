#ifndef KRISHNA_ARCH_X86_64_GDT_H
#define KRISHNA_ARCH_X86_64_GDT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Descriptor indexes:
 *
 * 0: null
 * 1: kernel code
 * 2: kernel data
 * 3: user data
 * 4: user code
 * 5-6: 64-bit TSS
 */
#define GDT_KERNEL_CODE_SELECTOR UINT16_C(0x08)
#define GDT_KERNEL_DATA_SELECTOR UINT16_C(0x10)

#define GDT_USER_DATA_SELECTOR   UINT16_C(0x1B)
#define GDT_USER_CODE_SELECTOR   UINT16_C(0x23)

#define GDT_TSS_SELECTOR         UINT16_C(0x28)

/*
 * Install KRISHNA's GDT and bootstrap-processor TSS.
 *
 * This function leaves maskable interrupts disabled.
 */
bool gdt_init(void);

/*
 * Verify GDTR, CS, SS and TR after installation.
 */
bool gdt_self_test(void);

/*
 * Set the ring-0 stack used when the CPU enters the kernel from
 * userspace.
 *
 * This will eventually be called whenever the scheduler selects a
 * userspace thread.
 */
bool gdt_set_kernel_stack(uint64_t stack_top);

uint64_t gdt_kernel_stack(void);

#endif