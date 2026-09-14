#ifndef KRISHNA_MEMORY_KERNEL_STACK_H
#define KRISHNA_MEMORY_KERNEL_STACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/kernel_pages.h"

struct kernel_stack {
    struct kernel_page_allocation allocation;
    uint64_t stack_bottom;
    uint64_t stack_top;
};

bool kernel_stack_allocate(
    size_t usable_pages,
    struct kernel_stack *stack
);

bool kernel_stack_release(
    struct kernel_stack *stack
);

bool kernel_stack_self_test(void);

#endif
