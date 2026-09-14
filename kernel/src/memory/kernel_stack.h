#ifndef KRISHNA_MEMORY_KERNEL_STACK_H
#define KRISHNA_MEMORY_KERNEL_STACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/kernel_pages.h"


struct kernel_stack {
    /*
     * Complete page-allocation descriptor, including both guards.
     */
    struct kernel_page_allocation allocation;

    /*
     * Lowest usable mapped address.
     */
    uint64_t stack_bottom;

    /*
     * Initial stack pointer.
     *
     * The stack grows downward from this address.
     */
    uint64_t stack_top;
};


/*
 * Create a zero-filled, non-executable kernel stack with one
 * unmapped guard page on each side.
 *
 * On failure, stack remains unchanged.
 */
bool kernel_stack_allocate(
    size_t usable_pages,
    struct kernel_stack *stack
);


/*
 * Release all mapped frames and the complete guarded virtual range.
 *
 * The descriptor is cleared only after successful release.
 */
bool kernel_stack_release(
    struct kernel_stack *stack
);


/*
 * Test stack layout, guard pages, mapped boundaries, access and
 * complete resource cleanup.
 */
bool kernel_stack_self_test(void);


#endif