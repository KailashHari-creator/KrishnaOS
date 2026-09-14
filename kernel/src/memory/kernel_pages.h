#ifndef KRISHNA_MEMORY_KERNEL_PAGES_H
#define KRISHNA_MEMORY_KERNEL_PAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/vmm.h"


/*
 * Describes one virtual reservation and its mapped usable pages.
 *
 * Guard pages belong to the reservation but remain unmapped.
 */
struct kernel_page_allocation {
    uint64_t reservation_base;
    uint64_t mapped_base;

    size_t mapped_pages;
    size_t guard_pages_before;
    size_t guard_pages_after;
};


/*
 * Reserve and map ordinary kernel pages.
 *
 * On success:
 *
 *     - every usable virtual page is mapped;
 *     - every physical frame is zero-filled;
 *     - allocation describes the completed allocation.
 *
 * On failure:
 *
 *     - all completed mappings are removed;
 *     - all physical frames are returned;
 *     - the virtual reservation is released;
 *     - allocation remains unchanged.
 */
bool kernel_pages_allocate(
    size_t page_count,
    size_t alignment_pages,
    uint64_t flags,
    struct kernel_page_allocation *allocation
);


/*
 * Allocate usable pages surrounded by optional unmapped guards.
 *
 * Alignment applies to the beginning of the complete reservation.
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
 * Unmap every usable page, return its physical frame and release
 * the complete virtual reservation.
 *
 * The descriptor is cleared only after successful release.
 */
bool kernel_pages_release(
    struct kernel_page_allocation *allocation
);


/*
 * Test allocation, mapping, zeroing, access, cleanup and rejection
 * of an invalid transaction.
 */
bool kernel_pages_self_test(void);


#endif