#include "memory/pmm.h"

#include <memory.h>

#include "sync/spinlock.h"

#define BITS_PER_BYTE UINT64_C(8)

static uint8_t *page_bitmap;
static uint64_t page_bitmap_physical;
static uint64_t page_bitmap_bytes;
static uint64_t page_bitmap_pages;

static uint64_t physical_page_count;
static uint64_t managed_page_count;
static uint64_t free_page_count;
static uint64_t next_search_page;
static uint64_t direct_map_offset;

static struct limine_memmap_response *limine_memory_map;
static bool manager_ready;

static spinlock_t allocator_lock =
    SPINLOCK_INITIALIZER;

static bool add_overflow_u64(
    uint64_t left,
    uint64_t right,
    uint64_t *result
)
{
    *result = left + right;
    return *result < left;
}

static uint64_t align_down(uint64_t value)
{
    return value & ~(PMM_PAGE_SIZE - 1);
}

static bool align_up(uint64_t value, uint64_t *result)
{
    uint64_t adjusted;

    if (add_overflow_u64(
            value,
            PMM_PAGE_SIZE - 1,
            &adjusted
        )) {
        return false;
    }

    *result = align_down(adjusted);
    return true;
}

static bool bitmap_test(uint64_t page)
{
    return (page_bitmap[page / BITS_PER_BYTE] &
            (uint8_t)(UINT8_C(1) << (page % BITS_PER_BYTE))) != 0;
}

static void bitmap_mark_used(uint64_t page)
{
    page_bitmap[page / BITS_PER_BYTE] |=
        (uint8_t)(UINT8_C(1) << (page % BITS_PER_BYTE));
}

static void bitmap_mark_free(uint64_t page)
{
    page_bitmap[page / BITS_PER_BYTE] &=
        (uint8_t)~(UINT8_C(1) << (page % BITS_PER_BYTE));
}

static bool page_is_managed(uint64_t page)
{
    uint64_t address = page * PMM_PAGE_SIZE;
    uint64_t page_end;

    if (add_overflow_u64(address, PMM_PAGE_SIZE, &page_end)) {
        return false;
    }

    for (uint64_t i = 0;
         i < limine_memory_map->entry_count;
         i++) {
        struct limine_memmap_entry *entry =
            limine_memory_map->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t entry_end;

        if (add_overflow_u64(
                entry->base,
                entry->length,
                &entry_end
            )) {
            continue;
        }

        if (address >= entry->base && page_end <= entry_end) {
            return true;
        }
    }

    return false;
}

static bool page_is_bitmap_storage(uint64_t page)
{
    uint64_t first = page_bitmap_physical / PMM_PAGE_SIZE;
    return page >= first && page < first + page_bitmap_pages;
}

static void release_usable_pages(void)
{
    for (uint64_t i = 0;
         i < limine_memory_map->entry_count;
         i++) {
        struct limine_memmap_entry *entry =
            limine_memory_map->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t start;
        uint64_t unaligned_end;

        if (!align_up(entry->base, &start) ||
            add_overflow_u64(
                entry->base,
                entry->length,
                &unaligned_end
            )) {
            continue;
        }

        uint64_t end = align_down(unaligned_end);

        if (end <= start) {
            continue;
        }

        uint64_t first_page = start / PMM_PAGE_SIZE;
        uint64_t final_page = end / PMM_PAGE_SIZE;

        for (uint64_t page = first_page;
             page < final_page;
             page++) {
            bitmap_mark_free(page);
            managed_page_count++;
            free_page_count++;
        }
    }
}

static bool choose_bitmap_region(uint64_t required_bytes)
{
    for (uint64_t i = 0;
         i < limine_memory_map->entry_count;
         i++) {
        struct limine_memmap_entry *entry =
            limine_memory_map->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t start;
        uint64_t unaligned_end;

        if (!align_up(entry->base, &start) ||
            add_overflow_u64(
                entry->base,
                entry->length,
                &unaligned_end
            )) {
            continue;
        }

        uint64_t end = align_down(unaligned_end);

        if (end > start && end - start >= required_bytes) {
            page_bitmap_physical = start;
            return true;
        }
    }

    return false;
}

bool pmm_init(
    struct limine_memmap_response *memory_map,
    uint64_t hhdm_offset
)
{
    if (manager_ready || memory_map == NULL ||
        memory_map->entry_count == 0) {
        return false;
    }

    uint64_t maximum_physical_address = 0;

    /*
    * Size the bitmap according to allocatable RAM, not the entire
    * sparse physical address space.
    *
    * QEMU may place MMIO regions at extremely high physical addresses.
    * Those addresses must not force us to create an enormous bitmap,
    * because the PMM never allocates from them.
    */
    for (uint64_t i = 0; i < memory_map->entry_count; i++) {
        struct limine_memmap_entry *entry =
            memory_map->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t end;

        if (add_overflow_u64(
                entry->base,
                entry->length,
                &end
            )) {
            return false;
        }

        if (end > maximum_physical_address) {
            maximum_physical_address = end;
        }
    }

    if (maximum_physical_address == 0) {
        return false;
    }

    uint64_t rounded_maximum;

    if (!align_up(maximum_physical_address, &rounded_maximum)) {
        return false;
    }

    physical_page_count = rounded_maximum / PMM_PAGE_SIZE;
    page_bitmap_bytes =
        (physical_page_count + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
    page_bitmap_pages =
        (page_bitmap_bytes + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;

    uint64_t bitmap_storage_bytes =
        page_bitmap_pages * PMM_PAGE_SIZE;

    limine_memory_map = memory_map;
    direct_map_offset = hhdm_offset;

    if (!choose_bitmap_region(bitmap_storage_bytes)) {
        limine_memory_map = NULL;
        return false;
    }

    if (page_bitmap_physical > UINT64_MAX - direct_map_offset) {
        limine_memory_map = NULL;
        return false;
    }

    page_bitmap = (uint8_t *)(
        page_bitmap_physical + direct_map_offset
    );

    /* Begin with every physical page reserved, including holes and MMIO. */
    memset(page_bitmap, 0xFF, bitmap_storage_bytes);

    release_usable_pages();

    /* Physical page zero remains reserved so zero is never a valid result. */
    if (physical_page_count != 0 && !bitmap_test(0)) {
        bitmap_mark_used(0);
        free_page_count--;
    }

    uint64_t bitmap_first_page =
        page_bitmap_physical / PMM_PAGE_SIZE;

    for (uint64_t page = bitmap_first_page;
         page < bitmap_first_page + page_bitmap_pages;
         page++) {
        if (!bitmap_test(page)) {
            bitmap_mark_used(page);
            free_page_count--;
        }
    }

    next_search_page = 1;
    spinlock_init(&allocator_lock);
    manager_ready = true;
    return true;
}

uint64_t pmm_allocate_pages(
    size_t requested_pages
)
{
    if (!manager_ready ||
        requested_pages == 0) {
        return PMM_INVALID_ADDRESS;
    }

    uint64_t needed =
        (uint64_t)requested_pages;

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &allocator_lock
        );

    if (needed > free_page_count) {
        spinlock_unlock_irqrestore(
            &allocator_lock,
            interrupt_state
        );

        return PMM_INVALID_ADDRESS;
    }

    uint64_t run_start = 0;
    uint64_t run_length = 0;

    /*
     * Two passes preserve next-fit behaviour while still allowing
     * allocation from holes located before next_search_page.
     */
    for (uint64_t pass = 0;
         pass < 2;
         pass++) {
        uint64_t begin =
            pass == 0
                ? next_search_page
                : 1;

        uint64_t end =
            pass == 0
                ? physical_page_count
                : next_search_page;

        run_length = 0;

        for (uint64_t page = begin;
             page < end;
             page++) {
            if (bitmap_test(page)) {
                run_length = 0;
                continue;
            }

            if (run_length == 0) {
                run_start = page;
            }

            run_length++;

            if (run_length != needed) {
                continue;
            }

            for (uint64_t allocated = run_start;
                 allocated < run_start + needed;
                 allocated++) {
                bitmap_mark_used(allocated);
            }

            free_page_count -= needed;
            next_search_page =
                run_start + needed;

            if (next_search_page >=
                physical_page_count) {
                next_search_page = 1;
            }

            uint64_t physical_address =
                run_start * PMM_PAGE_SIZE;

            spinlock_unlock_irqrestore(
                &allocator_lock,
                interrupt_state
            );

            return physical_address;
        }
    }

    spinlock_unlock_irqrestore(
        &allocator_lock,
        interrupt_state
    );

    return PMM_INVALID_ADDRESS;
}

uint64_t pmm_allocate_page(void)
{
    return pmm_allocate_pages(1);
}

bool pmm_free_pages(
    uint64_t physical_address,
    size_t requested_pages
)
{
    if (!manager_ready ||
        requested_pages == 0 ||
        (physical_address &
         (PMM_PAGE_SIZE - 1)) != 0) {
        return false;
    }

    uint64_t first_page =
        physical_address / PMM_PAGE_SIZE;

    uint64_t count =
        (uint64_t)requested_pages;

    if (first_page >= physical_page_count ||
        count >
            physical_page_count -
                first_page) {
        return false;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &allocator_lock
        );

    /*
     * Validate the entire range before changing the bitmap.
     * A failed multi-page free therefore changes nothing.
     */
    for (uint64_t page = first_page;
         page < first_page + count;
         page++) {
        if (!page_is_managed(page) ||
            page_is_bitmap_storage(page) ||
            !bitmap_test(page)) {
            spinlock_unlock_irqrestore(
                &allocator_lock,
                interrupt_state
            );

            return false;
        }
    }

    for (uint64_t page = first_page;
         page < first_page + count;
         page++) {
        bitmap_mark_free(page);
    }

    free_page_count += count;

    if (first_page < next_search_page) {
        next_search_page =
            first_page;
    }

    spinlock_unlock_irqrestore(
        &allocator_lock,
        interrupt_state
    );

    return true;
}

bool pmm_free_page(uint64_t physical_address)
{
    return pmm_free_pages(physical_address, 1);
}

bool pmm_is_allocated(
    uint64_t physical_address
)
{
    if (!manager_ready ||
        (physical_address &
         (PMM_PAGE_SIZE - 1)) != 0) {
        return true;
    }

    uint64_t page =
        physical_address / PMM_PAGE_SIZE;

    if (page >= physical_page_count) {
        return true;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &allocator_lock
        );

    bool allocated =
        bitmap_test(page);

    spinlock_unlock_irqrestore(
        &allocator_lock,
        interrupt_state
    );

    return allocated;
}

void pmm_get_statistics(
    struct pmm_statistics *statistics
)
{
    if (statistics == NULL) {
        return;
    }

    if (!manager_ready) {
        statistics->managed_pages = 0;
        statistics->free_pages = 0;
        statistics->used_pages = 0;
        statistics->bitmap_pages = 0;
        return;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &allocator_lock
        );

    statistics->managed_pages =
        managed_page_count;

    statistics->free_pages =
        free_page_count;

    statistics->used_pages =
        managed_page_count -
        free_page_count;

    statistics->bitmap_pages =
        page_bitmap_pages;

    spinlock_unlock_irqrestore(
        &allocator_lock,
        interrupt_state
    );
}

uint64_t pmm_hhdm_offset(void)
{
    return direct_map_offset;
}

void *pmm_physical_to_virtual(uint64_t physical_address)
{
    if (!manager_ready ||
        physical_address > UINT64_MAX - direct_map_offset) {
        return NULL;
    }

    return (void *)(physical_address + direct_map_offset);
}
