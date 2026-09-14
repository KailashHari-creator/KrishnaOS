#ifndef KRISHNA_MEMORY_LAYOUT_H
#define KRISHNA_MEMORY_LAYOUT_H

#include <stdint.h>

/*
 * KRISHNA OS currently uses conventional four-level x86-64 paging.
 *
 * Canonical virtual-address ranges:
 *
 * Lower half:
 *     0x0000000000000000 - 0x00007FFFFFFFFFFF
 *
 * Upper half:
 *     0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF
 *
 * Addresses between those ranges are non-canonical and cause a
 * general-protection fault if used.
 */

#define KRISHNA_PAGE_SIZE UINT64_C(0x1000)

/*
 * User address space
 * ------------------
 *
 * The lowest 4 MiB remains permanently unmapped. This catches null
 * pointers and small invalid addresses instead of silently allowing them.
 */
#define USER_NULL_GUARD_END \
    UINT64_C(0x00000000003FFFFF)

#define USER_SPACE_BASE \
    UINT64_C(0x0000000000400000)

#define USER_SPACE_TOP \
    UINT64_C(0x00007FFFFFFFF000)

/*
 * Kernel temporary-mapping region
 * --------------------------------
 *
 * Used when the kernel briefly needs to map a physical frame at a
 * controlled virtual address.
 */
#define KERNEL_TEMPORARY_BASE \
    UINT64_C(0xFFFFFD0000000000)

#define KERNEL_TEMPORARY_TOP \
    UINT64_C(0xFFFFFD7FFFFFFFFF)

/*
 * Memory-mapped device region
 * ---------------------------
 *
 * PCI BARs, APIC registers, and other MMIO devices will eventually
 * be mapped here with appropriate cache-control flags.
 */
#define KERNEL_MMIO_BASE \
    UINT64_C(0xFFFFFD8000000000)

#define KERNEL_MMIO_TOP \
    UINT64_C(0xFFFFFDFFFFFFFFFF)

/*
 * Kernel-stack region
 * -------------------
 *
 * Every kernel thread will eventually receive a stack in this region.
 * Unmapped guard pages will separate stacks.
 */
#define KERNEL_STACK_BASE \
    UINT64_C(0xFFFFFE0000000000)

#define KERNEL_STACK_TOP \
    UINT64_C(0xFFFFFE7FFFFFFFFF)

/*
 * Kernel heap region
 * ------------------
 *
 * kmalloc() will obtain virtual address space from this region and ask
 * the PMM/VMM to provide physical backing pages as required.
 */
#define KERNEL_HEAP_BASE \
    UINT64_C(0xFFFFFE8000000000)

#define KERNEL_HEAP_TOP \
    UINT64_C(0xFFFFFEFFFFFFFFFF)

/*
 * Recursive page-table region
 * ---------------------------
 *
 * Reserved for an optional recursive PML4 mapping. It provides a
 * convenient virtual view of page tables without exposing them to users.
 */
#define KERNEL_PAGE_TABLE_BASE \
    UINT64_C(0xFFFFFF0000000000)

#define KERNEL_PAGE_TABLE_TOP \
    UINT64_C(0xFFFFFF7FFFFFFFFF)

/*
 * Kernel-image region
 * -------------------
 *
 * The linker script currently places KRISHNA OS at:
 *
 *     0xFFFFFFFF80000000
 */
#define KERNEL_IMAGE_BASE \
    UINT64_C(0xFFFFFFFF80000000)

/*
 * The final page below the kernel's upper canonical-address boundary.
 */
#define KERNEL_ADDRESS_TOP \
    UINT64_C(0xFFFFFFFFFFFFF000)

#endif