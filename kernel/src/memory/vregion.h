#ifndef KRISHNA_MEMORY_VREGION_H
#define KRISHNA_MEMORY_VREGION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VREGION_INVALID_ADDRESS UINT64_MAX

/*
 * Initialise the allocator for KRISHNA OS's kernel-heap
 * virtual-address region.
 */
bool kernel_vregion_init(void);

/*
 * Reserve consecutive virtual pages.
 *
 * alignment_pages must be a power of two.
 * For ordinary allocation, use alignment_pages = 1.
 */
uint64_t kernel_vregion_reserve(
    size_t page_count,
    size_t alignment_pages
);

/*
 * Release a previously reserved virtual range.
 *
 * This does not unmap pages or free physical frames. Those operations
 * are the responsibility of the subsystem owning the mapping.
 */
bool kernel_vregion_release(
    uint64_t virtual_address,
    size_t page_count
);

uint64_t kernel_vregion_free_pages(void);

/*
 * Exercise range splitting, aligned allocation, releasing and
 * coalescing.
 */
bool kernel_vregion_self_test(void);

#endif