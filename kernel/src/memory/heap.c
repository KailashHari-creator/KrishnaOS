#include "memory/heap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <memory.h>

#include "memory/kernel_pages.h"
#include "memory/layout.h"
#include "sync/spinlock.h"

/*
 * The initial heap arena contains 16 pages = 64 KiB.
 *
 * Larger arenas will be created automatically when allocations
 * cannot fit in existing arenas.
 */
#define KHEAP_INITIAL_ARENA_PAGES 16

#define KHEAP_ALIGNMENT 16
#define KHEAP_MINIMUM_ALLOCATION 16

#define KHEAP_ARENA_MAGIC \
    UINT64_C(0x4B524953484E4141)

#define KHEAP_BLOCK_MAGIC \
    UINT64_C(0x4B524953484E4142)


struct heap_block {
    uint64_t magic;

    /*
     * Number of usable bytes following this header.
     */
    uint64_t size;

    struct heap_block *previous;
    struct heap_block *next;

    bool free;

    /*
     * Keep the structure size predictable without relying on
     * compiler-specific boolean padding.
     */
    uint8_t reserved[7];
};


struct heap_arena {
    uint64_t magic;
    uint64_t page_count;

    struct heap_arena *next;
    struct heap_block *first_block;
};


#define HEAP_BLOCK_HEADER_SIZE \
    ((sizeof(struct heap_block) + KHEAP_ALIGNMENT - 1) & \
     ~(KHEAP_ALIGNMENT - 1))

#define HEAP_ARENA_HEADER_SIZE \
    ((sizeof(struct heap_arena) + KHEAP_ALIGNMENT - 1) & \
     ~(KHEAP_ALIGNMENT - 1))


static struct heap_arena *first_arena;
static bool heap_initialized;

static uint64_t arena_count;
static uint64_t mapped_page_count;
static uint64_t allocated_block_count;
static uint64_t allocated_byte_count;

static spinlock_t heap_lock =
    SPINLOCK_INITIALIZER;


/*
 * Create a completely mapped and zero-filled page-backed arena.
 *
 * kernel_pages_allocate() owns the PMM/VMM/vregion transaction.
 * It either succeeds completely or rolls everything back.
 *
 * heap_lock must already be held.
 */
static struct heap_arena *create_arena(
    size_t requested_pages
)
{
    if (requested_pages == 0 ||
        requested_pages >
            UINT64_MAX /
                KRISHNA_PAGE_SIZE) {
        return NULL;
    }

    struct kernel_page_allocation allocation;

    if (!kernel_pages_allocate(
            requested_pages,
            1,
            VMM_PAGE_WRITABLE |
                VMM_PAGE_NO_EXECUTE,
            &allocation
        )) {
        return NULL;
    }

    uint64_t virtual_base =
        allocation.mapped_base;

    uint64_t arena_bytes =
        requested_pages *
            KRISHNA_PAGE_SIZE;

    /*
     * The allocation has already been zero-filled by
     * kernel_pages_allocate().
     */
    struct heap_arena *arena =
        (struct heap_arena *)(uintptr_t)
            virtual_base;

    arena->magic =
        KHEAP_ARENA_MAGIC;

    arena->page_count =
        requested_pages;

    arena->next = NULL;

    uint64_t first_block_address =
        virtual_base +
        HEAP_ARENA_HEADER_SIZE;

    struct heap_block *block =
        (struct heap_block *)(uintptr_t)
            first_block_address;

    block->magic =
        KHEAP_BLOCK_MAGIC;

    block->size =
        arena_bytes -
        HEAP_ARENA_HEADER_SIZE -
        HEAP_BLOCK_HEADER_SIZE;

    block->previous = NULL;
    block->next = NULL;
    block->free = true;

    arena->first_block = block;

    arena_count++;

    mapped_page_count +=
        requested_pages;

    return arena;
}

/*
 * Round an allocation up to the heap's 16-byte alignment.
 */
static bool align_allocation_size(
    size_t requested_size,
    uint64_t *aligned_size
)
{
    if (requested_size == 0 ||
        aligned_size == NULL) {
        return false;
    }

    uint64_t size =
        (uint64_t)requested_size;

    uint64_t mask =
        KHEAP_ALIGNMENT - 1;

    if (size > UINT64_MAX - mask) {
        return false;
    }

    *aligned_size =
        (size + mask) & ~mask;

    return true;
}

static bool kheap_init_locked(void)
{
    if (heap_initialized) {
        return false;
    }

    first_arena =
        create_arena(
            KHEAP_INITIAL_ARENA_PAGES
        );

    if (first_arena == NULL) {
        return false;
    }

    heap_initialized = true;
    return true;
}

bool kheap_init(void)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &heap_lock
        );

    bool initialized =
        kheap_init_locked();

    spinlock_unlock_irqrestore(
        &heap_lock,
        interrupt_state
    );

    return initialized;
}

static void kheap_get_statistics_locked(
    struct kheap_statistics *statistics
)
{
    if (statistics == NULL) {
        return;
    }

    *statistics =
        (struct kheap_statistics){0};

    if (!heap_initialized) {
        return;
    }

    statistics->arena_count =
        arena_count;

    statistics->mapped_pages =
        mapped_page_count;

    statistics->allocated_blocks =
        allocated_block_count;

    statistics->allocated_bytes =
        allocated_byte_count;

    struct heap_arena *arena =
        first_arena;

    while (arena != NULL) {
        if (arena->magic !=
            KHEAP_ARENA_MAGIC) {
            return;
        }

        struct heap_block *block =
            arena->first_block;

        while (block != NULL) {
            if (block->magic !=
                KHEAP_BLOCK_MAGIC) {
                return;
            }

            if (block->free) {
                statistics->free_blocks++;
                statistics->free_bytes +=
                    block->size;

                if (block->size >
                    statistics
                        ->largest_free_block) {
                    statistics
                        ->largest_free_block =
                        block->size;
                }
            }

            block = block->next;
        }

        arena = arena->next;
    }
}

void kheap_get_statistics(
    struct kheap_statistics *statistics
)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &heap_lock
        );

    kheap_get_statistics_locked(
        statistics
    );

    spinlock_unlock_irqrestore(
        &heap_lock,
        interrupt_state
    );
}

/*
 * Find the first free block large enough for the request.
 *
 * This is a first-fit allocator: it chooses the first suitable
 * block rather than scanning for the mathematically smallest one.
 */
static struct heap_block *find_free_block(
    uint64_t required_size
)
{
    struct heap_arena *arena =
        first_arena;

    while (arena != NULL) {
        if (arena->magic !=
            KHEAP_ARENA_MAGIC) {
            return NULL;
        }

        struct heap_block *block =
            arena->first_block;

        while (block != NULL) {
            if (block->magic !=
                KHEAP_BLOCK_MAGIC) {
                return NULL;
            }

            if (block->free &&
                block->size >= required_size) {
                return block;
            }

            block = block->next;
        }

        arena = arena->next;
    }

    return NULL;
}

/*
 * Split a free block when enough room remains for another header
 * and a useful allocation.
 */
static void split_block(
    struct heap_block *block,
    uint64_t required_size
)
{
    if (block == NULL ||
        block->size < required_size) {
        return;
    }

    uint64_t remaining =
        block->size - required_size;

    if (remaining <
        HEAP_BLOCK_HEADER_SIZE +
        KHEAP_MINIMUM_ALLOCATION) {
        /*
         * The remainder would be too small to use. Give the caller
         * the complete block instead.
         */
        return;
    }

    uint64_t new_block_address =
        (uint64_t)(uintptr_t)block +
        HEAP_BLOCK_HEADER_SIZE +
        required_size;

    struct heap_block *new_block =
        (struct heap_block *)(uintptr_t)
            new_block_address;

    new_block->magic =
        KHEAP_BLOCK_MAGIC;

    new_block->size =
        remaining -
        HEAP_BLOCK_HEADER_SIZE;

    new_block->previous = block;
    new_block->next = block->next;
    new_block->free = true;

    if (new_block->next != NULL) {
        new_block->next->previous =
            new_block;
    }

    block->next = new_block;
    block->size = required_size;
}
static void *allocate_from_block(
    struct heap_block *block,
    uint64_t required_size
)
{
    if (block == NULL ||
        !block->free ||
        block->size < required_size) {
        return NULL;
    }

    split_block(
        block,
        required_size
    );

    block->free = false;

    allocated_block_count++;
    allocated_byte_count +=
        block->size;

    return (void *)(uintptr_t)(
        (uint64_t)(uintptr_t)block +
        HEAP_BLOCK_HEADER_SIZE
    );
}
/*
 * Add an arena to the end of the heap's arena list.
 */
static void append_arena(
    struct heap_arena *new_arena
)
{
    if (new_arena == NULL) {
        return;
    }

    if (first_arena == NULL) {
        first_arena = new_arena;
        return;
    }

    struct heap_arena *arena =
        first_arena;

    while (arena->next != NULL) {
        arena = arena->next;
    }

    arena->next = new_arena;
}
/*
 * Calculate how many pages an arena needs to contain an allocation.
 */
static bool arena_pages_for_allocation(
    uint64_t allocation_size,
    size_t *required_pages
)
{
    if (required_pages == NULL) {
        return false;
    }

    uint64_t overhead =
        HEAP_ARENA_HEADER_SIZE +
        HEAP_BLOCK_HEADER_SIZE;

    if (allocation_size >
        UINT64_MAX - overhead) {
        return false;
    }

    uint64_t required_bytes =
        overhead + allocation_size;

    if (required_bytes >
        UINT64_MAX -
        (KRISHNA_PAGE_SIZE - 1)) {
        return false;
    }

    uint64_t page_count =
        (required_bytes +
         KRISHNA_PAGE_SIZE - 1) /
        KRISHNA_PAGE_SIZE;

    if (page_count <
        KHEAP_INITIAL_ARENA_PAGES) {
        page_count =
            KHEAP_INITIAL_ARENA_PAGES;
    }

    if (page_count > SIZE_MAX) {
        return false;
    }

    *required_pages =
        (size_t)page_count;

    return true;
}
static void *kmalloc_locked(size_t requested_size)
{
    if (!heap_initialized ||
        requested_size == 0) {
        return NULL;
    }

    uint64_t required_size;

    if (!align_allocation_size(
            requested_size,
            &required_size
        )) {
        return NULL;
    }

    /*
     * First try to reuse free space already mapped into the heap.
     */
    struct heap_block *block =
        find_free_block(required_size);

    if (block != NULL) {
        return allocate_from_block(
            block,
            required_size
        );
    }

    /*
     * Existing arenas are full. Create another page-backed arena.
     */
    size_t required_pages;

    if (!arena_pages_for_allocation(
            required_size,
            &required_pages
        )) {
        return NULL;
    }

    struct heap_arena *new_arena =
        create_arena(required_pages);

    if (new_arena == NULL) {
        return NULL;
    }

    append_arena(new_arena);

    return allocate_from_block(
        new_arena->first_block,
        required_size
    );
}

void *kmalloc(size_t requested_size)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &heap_lock
        );

    void *allocation =
        kmalloc_locked(requested_size);

    spinlock_unlock_irqrestore(
        &heap_lock,
        interrupt_state
    );

    return allocation;
}

/*
 * Find the exact block belonging to a payload pointer.
 *
 * We walk trusted heap metadata instead of immediately subtracting
 * a header from an arbitrary pointer. This rejects interior pointers
 * and addresses that do not belong to the heap.
 */
static bool find_heap_block(
    void *pointer,
    struct heap_arena **owner_arena,
    struct heap_block **owner_block
)
{
    if (pointer == NULL ||
        owner_arena == NULL ||
        owner_block == NULL) {
        return false;
    }

    uint64_t requested_address =
        (uint64_t)(uintptr_t)pointer;

    if ((requested_address &
         (KHEAP_ALIGNMENT - 1)) != 0) {
        return false;
    }

    struct heap_arena *arena =
        first_arena;

    while (arena != NULL) {
        if (arena->magic !=
            KHEAP_ARENA_MAGIC) {
            return false;
        }

        uint64_t arena_base =
            (uint64_t)(uintptr_t)arena;

        uint64_t arena_end =
            arena_base +
            arena->page_count *
                KRISHNA_PAGE_SIZE;

        struct heap_block *block =
            arena->first_block;

        struct heap_block *expected_previous =
            NULL;

        while (block != NULL) {
            uint64_t block_address =
                (uint64_t)(uintptr_t)block;

            if (block_address < arena_base ||
                block_address >= arena_end ||
                block->magic !=
                    KHEAP_BLOCK_MAGIC ||
                block->previous !=
                    expected_previous) {
                return false;
            }

            uint64_t payload_address =
                block_address +
                HEAP_BLOCK_HEADER_SIZE;

            if (block->size >
                arena_end - payload_address) {
                return false;
            }

            uint64_t block_end =
                payload_address +
                block->size;

            /*
             * Every block after this one must begin exactly where
             * the current block ends.
             */
            if (block->next != NULL &&
                (uint64_t)(uintptr_t)block->next !=
                    block_end) {
                return false;
            }

            if (payload_address ==
                requested_address) {
                *owner_arena = arena;
                *owner_block = block;
                return true;
            }

            expected_previous = block;
            block = block->next;
        }

        arena = arena->next;
    }

    return false;
}
/*
 * Allow block to absorb its following free block.
 *
 * block may itself be allocated, which is required for growing
 * an allocation in place during krealloc().
 */
static void merge_with_next(
    struct heap_block *block
)
{
    struct heap_block *next =
        block->next;

    if (next == NULL ||
        !next->free) {
        return;
    }

    block->size +=
        HEAP_BLOCK_HEADER_SIZE +
        next->size;

    block->next = next->next;

    if (block->next != NULL) {
        block->next->previous =
            block;
    }

    /*
     * Poison the absorbed header. A stale pointer cannot mistake
     * it for a valid allocation header.
     */
    next->magic = 0;
    next->size = 0;
    next->previous = NULL;
    next->next = NULL;
    next->free = false;
}
static bool arena_is_completely_free(
    struct heap_arena *arena
)
{
    if (arena == NULL ||
        arena->magic !=
            KHEAP_ARENA_MAGIC) {
        return false;
    }

    struct heap_block *block =
        arena->first_block;

    if (block == NULL ||
        block->magic !=
            KHEAP_BLOCK_MAGIC ||
        !block->free ||
        block->previous != NULL ||
        block->next != NULL) {
        return false;
    }

    uint64_t expected_size =
        arena->page_count *
            KRISHNA_PAGE_SIZE -
        HEAP_ARENA_HEADER_SIZE -
        HEAP_BLOCK_HEADER_SIZE;

    return block->size == expected_size;
}
/*
 * Return a completely unused secondary arena.
 *
 * The first arena remains mapped as the permanent bootstrap arena.
 *
 * heap_lock must already be held.
 */
static bool release_empty_arena(
    struct heap_arena *arena
)
{
    if (arena == NULL ||
        arena == first_arena ||
        !arena_is_completely_free(arena)) {
        return true;
    }

    struct heap_arena *previous =
        first_arena;

    while (previous != NULL &&
           previous->next != arena) {
        previous = previous->next;
    }

    if (previous == NULL) {
        return false;
    }

    /*
     * Save all metadata before releasing the mapping. Once the
     * first arena page is unmapped, arena itself becomes invalid.
     */
    uint64_t virtual_base =
        (uint64_t)(uintptr_t)arena;

    uint64_t pages =
        arena->page_count;

    struct heap_arena *next_arena =
        arena->next;

    if (pages == 0 ||
        pages > SIZE_MAX) {
        return false;
    }

    /*
     * Heap arenas have no guard pages, so their reservation and
     * mapped ranges are identical.
     */
    struct kernel_page_allocation allocation = {
        .reservation_base =
            virtual_base,

        .mapped_base =
            virtual_base,

        .mapped_pages =
            (size_t)pages,

        .guard_pages_before = 0,
        .guard_pages_after = 0
    };

    /*
     * Do not access arena after this succeeds: the memory holding
     * its header has been unmapped.
     */
    if (!kernel_pages_release(
            &allocation
        )) {
        return false;
    }

    /*
     * previous is part of a different arena and remains mapped.
     * Relink only after the backing pages have been released.
     */
    previous->next =
        next_arena;

    arena_count--;
    mapped_page_count -= pages;

    return true;
}
static bool kfree_locked(void *pointer)
{
    /*
     * Like standard free(), releasing NULL is harmless.
     */
    if (pointer == NULL) {
        return true;
    }

    if (!heap_initialized) {
        return false;
    }

    struct heap_arena *arena;
    struct heap_block *block;

    if (!find_heap_block(
            pointer,
            &arena,
            &block
        )) {
        return false;
    }

    /*
     * The block exists but has already been released.
     */
    if (block->free) {
        return false;
    }

    if (allocated_block_count == 0 ||
        allocated_byte_count <
            block->size) {
        return false;
    }

    allocated_block_count--;
    allocated_byte_count -=
        block->size;

    block->free = true;

    /*
     * First merge forward.
     */
    if (block->next != NULL &&
        block->next->free) {
        merge_with_next(block);
    }

    /*
     * Then merge backwards. The previous block absorbs this one.
     */
    if (block->previous != NULL &&
        block->previous->free) {
        block = block->previous;
        merge_with_next(block);
    }

    return release_empty_arena(arena);
}

bool kfree(void *pointer)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &heap_lock
        );

    bool released =
        kfree_locked(pointer);

    spinlock_unlock_irqrestore(
        &heap_lock,
        interrupt_state
    );

    return released;
}

void *kcalloc(
    size_t count,
    size_t size
)
{
    /*
     * KRISHNA OS returns NULL for zero-sized allocations.
     */
    if (count == 0 || size == 0) {
        return NULL;
    }

    /*
     * Prevent count * size from wrapping to a smaller number.
     */
    if (size > SIZE_MAX / count) {
        return NULL;
    }

    size_t total_size =
        count * size;

    void *allocation =
        kmalloc(total_size);

    if (allocation == NULL) {
        return NULL;
    }

    memset(
        allocation,
        0,
        total_size
    );

    return allocation;
}
static void *krealloc_locked(
    void *pointer,
    size_t requested_size
)
{
    /*
     * realloc(NULL, size) behaves like malloc(size).
     */
    if (pointer == NULL) {
        return kmalloc_locked(
            requested_size
        );
    }

    /*
     * realloc(pointer, 0) releases the allocation.
     */
    if (requested_size == 0) {
        kfree_locked(pointer);
        return NULL;
    }

    if (!heap_initialized) {
        return NULL;
    }

    struct heap_arena *arena;
    struct heap_block *block;

    if (!find_heap_block(
            pointer,
            &arena,
            &block
        )) {
        return NULL;
    }

    (void)arena;

    if (block->free) {
        return NULL;
    }

    uint64_t required_size;

    if (!align_allocation_size(
            requested_size,
            &required_size
        )) {
        return NULL;
    }

    uint64_t old_size =
        block->size;

    /*
    * Shrink the existing block.
    */
    if (required_size <= old_size) {
        split_block(
            block,
            required_size
        );

        /*
        * Splitting an allocated block can place the new free remainder
        * immediately before another free block. Coalesce them to maintain
        * the invariant that adjacent free blocks never exist.
        */
        struct heap_block *remainder =
            block->next;

        if (remainder != NULL &&
            remainder->free) {
            while (remainder->next != NULL &&
                remainder->next->free) {
                merge_with_next(remainder);
            }
        }

        allocated_byte_count -= old_size;
        allocated_byte_count += block->size;

        return pointer;
    }

    /*
     * Attempt to grow into the following free block.
     */
    if (block->next != NULL &&
        block->next->free) {
        struct heap_block *next =
            block->next;

        bool addition_safe =
            next->size <=
                UINT64_MAX -
                old_size -
                HEAP_BLOCK_HEADER_SIZE;

        if (addition_safe) {
            uint64_t combined_size =
                old_size +
                HEAP_BLOCK_HEADER_SIZE +
                next->size;

            if (combined_size >=
                required_size) {
                merge_with_next(block);

                split_block(
                    block,
                    required_size
                );

                allocated_byte_count -=
                    old_size;

                allocated_byte_count +=
                    block->size;

                return pointer;
            }
        }
    }

    /*
     * The allocation cannot grow in place. Create a new allocation,
     * copy the old contents and release the old block.
     */
    void *new_pointer =
        kmalloc_locked(requested_size);

    if (new_pointer == NULL) {
        /*
         * Standard realloc semantics require the original allocation
         * to remain valid when resizing fails.
         */
        return NULL;
    }

    memcpy(
        new_pointer,
        pointer,
        old_size
    );

    if (!kfree_locked(pointer)) {
        /*
         * This indicates heap corruption. Release the replacement
         * so we do not leak another allocation.
         */
        kfree_locked(new_pointer);
        return NULL;
    }

    return new_pointer;
}

void *krealloc(
    void *pointer,
    size_t requested_size
)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &heap_lock
        );

    void *allocation =
        krealloc_locked(
            pointer,
            requested_size
        );

    spinlock_unlock_irqrestore(
        &heap_lock,
        interrupt_state
    );

    return allocation;
}

bool kheap_self_test(void)
{
    if (!heap_initialized) {
        return false;
    }

    struct kheap_statistics before;
    struct kheap_statistics after;

    kheap_get_statistics(&before);

    uint8_t *small =
        kmalloc(24);

    uint8_t *medium =
        kmalloc(4096);

    uint64_t *zeroed =
        kcalloc(128, sizeof(uint64_t));

    /*
     * Force creation of a secondary arena.
     */
    uint8_t *large =
        kmalloc(128 * 1024);

    bool allocation_passed =
        small != NULL &&
        medium != NULL &&
        zeroed != NULL &&
        large != NULL;

    bool alignment_passed =
        allocation_passed &&
        ((uintptr_t)small &
         (KHEAP_ALIGNMENT - 1)) == 0 &&
        ((uintptr_t)medium &
         (KHEAP_ALIGNMENT - 1)) == 0 &&
        ((uintptr_t)zeroed &
         (KHEAP_ALIGNMENT - 1)) == 0 &&
        ((uintptr_t)large &
         (KHEAP_ALIGNMENT - 1)) == 0;

    bool zeroing_passed =
        zeroed != NULL;

    if (zeroed != NULL) {
        for (size_t index = 0;
             index < 128;
             index++) {
            if (zeroed[index] != 0) {
                zeroing_passed = false;
                break;
            }
        }
    }

    bool write_read_passed = false;

    if (allocation_passed) {
        for (size_t index = 0;
             index < 24;
             index++) {
            small[index] =
                (uint8_t)(index ^ 0xA5);
        }

        medium[0] = 0x33;
        medium[4095] = 0x44;

        large[0] = 0x55;
        large[(128 * 1024) - 1] =
            0x66;

        zeroed[64] =
            UINT64_C(0x4B524953484E4121);

        write_read_passed =
            medium[0] == 0x33 &&
            medium[4095] == 0x44 &&
            large[0] == 0x55 &&
            large[(128 * 1024) - 1] ==
                0x66 &&
            zeroed[64] ==
                UINT64_C(0x4B524953484E4121);
    }

    /*
     * The next allocated block prevents small from growing directly,
     * forcing krealloc() to move and copy it.
     */
    uint8_t *resized = NULL;
    bool growth_passed = false;

    if (small != NULL) {
        resized =
            krealloc(small, 512);

        if (resized != NULL) {
            small = NULL;
            growth_passed = true;

            for (size_t index = 0;
                 index < 24;
                 index++) {
                uint8_t expected =
                    (uint8_t)(index ^ 0xA5);

                if (resized[index] !=
                    expected) {
                    growth_passed = false;
                    break;
                }
            }
        }
    }

    bool shrinking_passed = false;

    if (resized != NULL) {
        uint8_t *shrunk =
            krealloc(resized, 32);

        if (shrunk != NULL) {
            resized = shrunk;
            shrinking_passed = true;

            for (size_t index = 0;
                 index < 24;
                 index++) {
                uint8_t expected =
                    (uint8_t)(index ^ 0xA5);

                if (resized[index] !=
                    expected) {
                    shrinking_passed = false;
                    break;
                }
            }
        }
    }

    bool overflow_rejected =
        kcalloc(SIZE_MAX, 2) == NULL;

    bool release_passed = true;
    bool double_free_rejected = false;

    if (medium != NULL) {
        release_passed &=
            kfree(medium);

        double_free_rejected =
            !kfree(medium);

        medium = NULL;
    }

    if (zeroed != NULL) {
        release_passed &=
            kfree(zeroed);

        zeroed = NULL;
    }

    if (large != NULL) {
        release_passed &=
            kfree(large);

        large = NULL;
    }

    if (resized != NULL) {
        release_passed &=
            kfree(resized);

        resized = NULL;
    }

    /*
     * If growing small failed, its original allocation remains valid.
     */
    if (small != NULL) {
        release_passed &=
            kfree(small);

        small = NULL;
    }

    kheap_get_statistics(&after);

    bool cleanup_passed =
        before.arena_count ==
            after.arena_count &&
        before.mapped_pages ==
            after.mapped_pages &&
        before.allocated_blocks ==
            after.allocated_blocks &&
        before.allocated_bytes ==
            after.allocated_bytes &&
        before.free_blocks ==
            after.free_blocks &&
        before.free_bytes ==
            after.free_bytes;

    return allocation_passed &&
        alignment_passed &&
        zeroing_passed &&
        write_read_passed &&
        growth_passed &&
        shrinking_passed &&
        overflow_rejected &&
        release_passed &&
        double_free_rejected &&
        cleanup_passed;
}