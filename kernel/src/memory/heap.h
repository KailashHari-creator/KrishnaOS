#ifndef KRISHNA_MEMORY_HEAP_H
#define KRISHNA_MEMORY_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Current state of the kernel heap.
 */
struct kheap_statistics {
    uint64_t arena_count;
    uint64_t mapped_pages;

    uint64_t allocated_blocks;
    uint64_t allocated_bytes;

    uint64_t free_blocks;
    uint64_t free_bytes;
    uint64_t largest_free_block;
};


/*
 * Create the first page-backed kernel heap arena.
 *
 * Requirements:
 *
 *     PMM initialized
 *     VMM initialized
 *     KRISHNA page tables active
 *     kernel_vregion initialized
 */
bool kheap_init(void);


/*
 * Allocate at least size bytes.
 *
 * Returned addresses are aligned to 16 bytes.
 * Returns NULL if size is zero or allocation fails.
 */
void *kmalloc(size_t size);


/*
 * Allocate count * size bytes and initialize them to zero.
 *
 * Returns NULL if multiplication overflows.
 */
void *kcalloc(
    size_t count,
    size_t size
);


/*
 * Resize an allocation.
 *
 * Existing contents are preserved up to the smaller of the old
 * and new sizes.
 *
 * pointer == NULL behaves like kmalloc().
 * size == 0 frees pointer and returns NULL.
 */
void *krealloc(
    void *pointer,
    size_t size
);


/*
 * Release a kernel allocation.
 *
 * Returns false for an invalid pointer or double free.
 * Passing NULL succeeds without performing any operation.
 */
bool kfree(void *pointer);


/*
 * Collect current heap statistics.
 */
void kheap_get_statistics(
    struct kheap_statistics *statistics
);


/*
 * Test allocation, alignment, zeroing, resizing, preservation,
 * freeing, coalescing and leak-free cleanup.
 */
bool kheap_self_test(void);

#endif