#ifndef KRISHNA_MEMORY_KERNEL_PAGES_H
#define KRISHNA_MEMORY_KERNEL_PAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/vmm.h"

struct kernel_page_allocation {
    uint64_t reservation_base;
    uint64_t mapped_base;
    size_t mapped_pages;
    size_t guard_pages_before;
    size_t guard_pages_after;
};

/*
 * Reserve virtual space and back every page with an independently
 * allocated physical frame. On failure, all completed work is rolled
 * back and allocation remains unchanged.
 */
bool kernel_pages_allocate(
    size_t page_count,
    size_t alignment_pages,
    uint64_t flags,
    struct kernel_page_allocation *allocation
);

/*
 * As above, but leave guard pages unmapped before and after the
 * usable range. Alignment applies to the complete reservation.
 */
bool kernel_pages_allocate_guarded(
    size_t page_count,
    size_t alignment_pages,
    size_t guard_pages_before,
    size_t guard_pages_after,
    uint64_t flags,
    struct kernel_page_allocation *allocation
);

/*
 * Unmap and free a complete allocation, including its virtual
 * reservation. The descriptor is cleared only after success.
 */
bool kernel_pages_release(
    struct kernel_page_allocation *allocation
);

bool kernel_pages_self_test(void);

#endif
