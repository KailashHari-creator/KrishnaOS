#include "memory/vregion.h"

#include <stdint.h>

#include "memory/layout.h"
#include "sync/spinlock.h"

#define MAX_FREE_EXTENTS 256

struct virtual_extent {
    uint64_t start;
    uint64_t page_count;
};

static struct virtual_extent free_extents[MAX_FREE_EXTENTS];
static size_t free_extent_count;

static uint64_t available_pages;
static bool allocator_initialized;

static spinlock_t allocator_lock =
    SPINLOCK_INITIALIZER;


static bool is_power_of_two(uint64_t value)
{
    return value != 0 &&
        (value & (value - 1)) == 0;
}


static bool add_overflow_u64(
    uint64_t left,
    uint64_t right,
    uint64_t *result
)
{
    *result = left + right;
    return *result < left;
}


static bool align_up(
    uint64_t value,
    uint64_t alignment,
    uint64_t *result
)
{
    uint64_t mask = alignment - 1;

    if (value > UINT64_MAX - mask) {
        return false;
    }

    *result = (value + mask) & ~mask;
    return true;
}


static void remove_extent(size_t index)
{
    for (size_t i = index;
         i + 1 < free_extent_count;
         i++) {
        free_extents[i] =
            free_extents[i + 1];
    }

    free_extent_count--;
}


static bool kernel_vregion_init_locked(void)
{
    if (allocator_initialized) {
        return false;
    }

    if ((KERNEL_HEAP_BASE &
         (KRISHNA_PAGE_SIZE - 1)) != 0) {
        return false;
    }

    uint64_t region_size =
        KERNEL_HEAP_TOP -
        KERNEL_HEAP_BASE + 1;

    if ((region_size &
         (KRISHNA_PAGE_SIZE - 1)) != 0) {
        return false;
    }

    free_extents[0].start =
        KERNEL_HEAP_BASE;

    free_extents[0].page_count =
        region_size / KRISHNA_PAGE_SIZE;

    free_extent_count = 1;
    available_pages =
        free_extents[0].page_count;

    allocator_initialized = true;

    return true;
}


bool kernel_vregion_init(void)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(&allocator_lock);

    bool initialized =
        kernel_vregion_init_locked();

    spinlock_unlock_irqrestore(
        &allocator_lock,
        interrupt_state
    );

    return initialized;
}

static uint64_t kernel_vregion_reserve_locked(
    size_t requested_pages,
    size_t requested_alignment_pages
)
{
    if (!allocator_initialized ||
        requested_pages == 0 ||
        requested_alignment_pages == 0) {
        return VREGION_INVALID_ADDRESS;
    }

    uint64_t page_count =
        (uint64_t)requested_pages;

    uint64_t alignment_pages =
        (uint64_t)requested_alignment_pages;

    if (!is_power_of_two(alignment_pages) ||
        page_count > UINT64_MAX / KRISHNA_PAGE_SIZE ||
        alignment_pages >
            UINT64_MAX / KRISHNA_PAGE_SIZE) {
        return VREGION_INVALID_ADDRESS;
    }

    uint64_t alignment =
        alignment_pages * KRISHNA_PAGE_SIZE;


    if (page_count > available_pages) {
        return VREGION_INVALID_ADDRESS;
    }

    for (size_t index = 0;
         index < free_extent_count;
         index++) {
        struct virtual_extent extent =
            free_extents[index];

        uint64_t aligned_start;

        if (!align_up(
                extent.start,
                alignment,
                &aligned_start
            )) {
            continue;
        }

        uint64_t prefix_pages =
            (aligned_start - extent.start) /
            KRISHNA_PAGE_SIZE;

        if (prefix_pages > extent.page_count) {
            continue;
        }

        uint64_t remaining_pages =
            extent.page_count - prefix_pages;

        if (page_count > remaining_pages) {
            continue;
        }

        uint64_t suffix_pages =
            remaining_pages - page_count;

        /*
         * Allocation consumes the complete extent.
         */
        if (prefix_pages == 0 &&
            suffix_pages == 0) {
            remove_extent(index);
        }

        /*
         * Allocation consumes the beginning.
         */
        else if (prefix_pages == 0) {
            free_extents[index].start =
                aligned_start +
                page_count * KRISHNA_PAGE_SIZE;

            free_extents[index].page_count =
                suffix_pages;
        }

        /*
         * Allocation consumes the end.
         */
        else if (suffix_pages == 0) {
            free_extents[index].page_count =
                prefix_pages;
        }

        /*
         * Allocation splits one free extent into two.
         */
        else {
            if (free_extent_count >=
                MAX_FREE_EXTENTS) {
                return VREGION_INVALID_ADDRESS;
            }

            for (size_t move = free_extent_count;
                 move > index + 1;
                 move--) {
                free_extents[move] =
                    free_extents[move - 1];
            }

            free_extents[index].page_count =
                prefix_pages;

            free_extents[index + 1].start =
                aligned_start +
                page_count * KRISHNA_PAGE_SIZE;

            free_extents[index + 1].page_count =
                suffix_pages;

            free_extent_count++;
        }

        available_pages -= page_count;
        return aligned_start;
    }
    return VREGION_INVALID_ADDRESS;
}


uint64_t kernel_vregion_reserve(
    size_t requested_pages,
    size_t requested_alignment_pages
)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(&allocator_lock);

    uint64_t address =
        kernel_vregion_reserve_locked(
            requested_pages,
            requested_alignment_pages
        );

    spinlock_unlock_irqrestore(
        &allocator_lock,
        interrupt_state
    );

    return address;
}

static bool kernel_vregion_release_locked(
    uint64_t virtual_address,
    size_t requested_pages
)
{
    if (!allocator_initialized ||
        requested_pages == 0 ||
        (virtual_address &
         (KRISHNA_PAGE_SIZE - 1)) != 0) {
        return false;
    }

    uint64_t page_count =
        (uint64_t)requested_pages;

    if (page_count >
        UINT64_MAX / KRISHNA_PAGE_SIZE) {
        return false;
    }

    uint64_t byte_count =
        page_count * KRISHNA_PAGE_SIZE;

    uint64_t range_end;

    if (add_overflow_u64(
            virtual_address,
            byte_count,
            &range_end
        )) {
        return false;
    }

    uint64_t heap_end =
        KERNEL_HEAP_TOP + 1;

    if (virtual_address < KERNEL_HEAP_BASE ||
        range_end > heap_end) {
        return false;
    }


    size_t position = 0;

    while (position < free_extent_count &&
           free_extents[position].start <
               virtual_address) {
        position++;
    }

    /*
     * Reject overlap with the previous free extent.
     * This also catches double frees.
     */
    if (position > 0) {
        struct virtual_extent *previous =
            &free_extents[position - 1];

        uint64_t previous_end =
            previous->start +
            previous->page_count *
                KRISHNA_PAGE_SIZE;

        if (previous_end > virtual_address) {
            return false;
        }
    }

    /*
     * Reject overlap with the next free extent.
     */
    if (position < free_extent_count &&
        range_end >
            free_extents[position].start) {
        return false;
    }

    bool touches_previous = false;
    bool touches_next = false;

    if (position > 0) {
        struct virtual_extent *previous =
            &free_extents[position - 1];

        uint64_t previous_end =
            previous->start +
            previous->page_count *
                KRISHNA_PAGE_SIZE;

        touches_previous =
            previous_end == virtual_address;
    }

    if (position < free_extent_count) {
        touches_next =
            range_end ==
            free_extents[position].start;
    }

    /*
     * Join the released range with both neighbours.
     */
    if (touches_previous && touches_next) {
        struct virtual_extent *previous =
            &free_extents[position - 1];

        previous->page_count +=
            page_count +
            free_extents[position].page_count;

        remove_extent(position);
    }

    /*
     * Extend the previous range.
     */
    else if (touches_previous) {
        free_extents[position - 1].page_count +=
            page_count;
    }

    /*
     * Extend the next range backwards.
     */
    else if (touches_next) {
        free_extents[position].start =
            virtual_address;

        free_extents[position].page_count +=
            page_count;
    }

    /*
     * Insert an independent free range.
     */
    else {
        if (free_extent_count >=
            MAX_FREE_EXTENTS) {
            return false;
        }

        for (size_t move = free_extent_count;
             move > position;
             move--) {
            free_extents[move] =
                free_extents[move - 1];
        }

        free_extents[position].start =
            virtual_address;

        free_extents[position].page_count =
            page_count;

        free_extent_count++;
    }

    available_pages += page_count;
    return true;
}


bool kernel_vregion_release(
    uint64_t virtual_address,
    size_t requested_pages
)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(&allocator_lock);

    bool released =
        kernel_vregion_release_locked(
            virtual_address,
            requested_pages
        );

    spinlock_unlock_irqrestore(
        &allocator_lock,
        interrupt_state
    );

    return released;
}

static uint64_t kernel_vregion_free_pages_locked(void)
{
    if (!allocator_initialized) {
        return 0;
    }


    uint64_t result =
        available_pages;
    return result;
}


uint64_t kernel_vregion_free_pages(void)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(&allocator_lock);

    uint64_t pages =
        kernel_vregion_free_pages_locked();

    spinlock_unlock_irqrestore(
        &allocator_lock,
        interrupt_state
    );

    return pages;
}

bool kernel_vregion_self_test(void)
{
    uint64_t pages_before =
        kernel_vregion_free_pages();

    uint64_t first =
        kernel_vregion_reserve(1, 1);

    uint64_t second =
        kernel_vregion_reserve(3, 1);

    /*
     * This allocation must begin at a virtual address aligned
     * to sixteen pages, or 64 KiB.
     */
    uint64_t aligned =
        kernel_vregion_reserve(8, 16);

    bool allocation_passed =
        first != VREGION_INVALID_ADDRESS &&
        second != VREGION_INVALID_ADDRESS &&
        aligned != VREGION_INVALID_ADDRESS;

    bool alignment_passed =
        aligned != VREGION_INVALID_ADDRESS &&
        (aligned &
         ((16 * KRISHNA_PAGE_SIZE) - 1)) == 0;

    bool release_passed = true;

    /*
     * Release in a deliberately unusual order. This verifies
     * that neighbouring free extents are coalesced correctly.
     */
    if (second != VREGION_INVALID_ADDRESS) {
        release_passed &=
            kernel_vregion_release(second, 3);
    }

    if (first != VREGION_INVALID_ADDRESS) {
        release_passed &=
            kernel_vregion_release(first, 1);
    }

    if (aligned != VREGION_INVALID_ADDRESS) {
        release_passed &=
            kernel_vregion_release(aligned, 8);
    }

    uint64_t pages_after =
        kernel_vregion_free_pages();

    return allocation_passed &&
        alignment_passed &&
        release_passed &&
        pages_before == pages_after;
}