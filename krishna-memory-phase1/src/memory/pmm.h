#ifndef KRISHNA_MEMORY_PMM_H
#define KRISHNA_MEMORY_PMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <limine.h>

#define PMM_PAGE_SIZE UINT64_C(4096)
#define PMM_INVALID_ADDRESS UINT64_MAX

struct pmm_statistics {
    uint64_t managed_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t bitmap_pages;
};

/*
 * Initialise the physical-memory manager from Limine's memory map.
 * Only LIMINE_MEMMAP_USABLE pages become allocatable. The allocator bitmap
 * is placed inside, and reserved from, one of those usable regions.
 */
bool pmm_init(
    struct limine_memmap_response *memory_map,
    uint64_t hhdm_offset
);

uint64_t pmm_allocate_page(void);
uint64_t pmm_allocate_pages(size_t page_count);

bool pmm_free_page(uint64_t physical_address);
bool pmm_free_pages(uint64_t physical_address, size_t page_count);

bool pmm_is_allocated(uint64_t physical_address);
void pmm_get_statistics(struct pmm_statistics *statistics);

uint64_t pmm_hhdm_offset(void);
void *pmm_physical_to_virtual(uint64_t physical_address);

#endif
