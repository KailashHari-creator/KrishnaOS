#include "memory/kernel_pages.h"

#include <stddef.h>
#include <stdint.h>

#include <memory.h>

#include "memory/layout.h"
#include "memory/pmm.h"
#include "memory/vregion.h"

static bool add_size(
    size_t left,
    size_t right,
    size_t *result
)
{
    if (left > SIZE_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static void rollback_mapping(
    struct vmm_address_space *space,
    uint64_t reservation_base,
    size_t reservation_pages,
    uint64_t mapped_base,
    size_t mapped_pages
)
{
    while (mapped_pages > 0) {
        mapped_pages--;

        uint64_t physical_address;

        if (vmm_unmap_page(
                space,
                mapped_base +
                    mapped_pages * KRISHNA_PAGE_SIZE,
                &physical_address
            )) {
            pmm_free_page(physical_address);
        }
    }

    kernel_vregion_release(
        reservation_base,
        reservation_pages
    );
}

bool kernel_pages_allocate_guarded(
    size_t page_count,
    size_t alignment_pages,
    size_t guard_pages_before,
    size_t guard_pages_after,
    uint64_t flags,
    struct kernel_page_allocation *allocation
)
{
    if (allocation == NULL ||
        page_count == 0 ||
        alignment_pages == 0) {
        return false;
    }

    size_t reservation_pages;

    if (!add_size(
            page_count,
            guard_pages_before,
            &reservation_pages
        ) ||
        !add_size(
            reservation_pages,
            guard_pages_after,
            &reservation_pages
        ) ||
        reservation_pages >
            UINT64_MAX / KRISHNA_PAGE_SIZE ||
        guard_pages_before >
            UINT64_MAX / KRISHNA_PAGE_SIZE) {
        return false;
    }

    uint64_t reservation_base =
        kernel_vregion_reserve(
            reservation_pages,
            alignment_pages
        );

    if (reservation_base ==
        VREGION_INVALID_ADDRESS) {
        return false;
    }

    uint64_t mapped_base =
        reservation_base +
        guard_pages_before *
            KRISHNA_PAGE_SIZE;

    struct vmm_address_space *space =
        vmm_kernel_address_space();

    if (space == NULL) {
        kernel_vregion_release(
            reservation_base,
            reservation_pages
        );
        return false;
    }

    size_t mapped_pages = 0;
    uint64_t temporary_flags =
        flags | VMM_PAGE_WRITABLE;

    while (mapped_pages < page_count) {
        uint64_t physical_address =
            pmm_allocate_page();

        if (physical_address ==
            PMM_INVALID_ADDRESS) {
            rollback_mapping(
                space,
                reservation_base,
                reservation_pages,
                mapped_base,
                mapped_pages
            );
            return false;
        }

        uint64_t virtual_address =
            mapped_base +
            mapped_pages *
                KRISHNA_PAGE_SIZE;

        if (!vmm_map_page(
                space,
                virtual_address,
                physical_address,
                temporary_flags
            )) {
            pmm_free_page(physical_address);

            rollback_mapping(
                space,
                reservation_base,
                reservation_pages,
                mapped_base,
                mapped_pages
            );
            return false;
        }

        mapped_pages++;
    }

    memset(
        (void *)(uintptr_t)mapped_base,
        0,
        page_count * KRISHNA_PAGE_SIZE
    );

    if ((flags & VMM_PAGE_WRITABLE) == 0) {
        size_t protected_pages = 0;

        while (protected_pages < page_count) {
            if (!vmm_protect_page(
                    space,
                    mapped_base +
                        protected_pages *
                            KRISHNA_PAGE_SIZE,
                    flags
                )) {
                rollback_mapping(
                    space,
                    reservation_base,
                    reservation_pages,
                    mapped_base,
                    mapped_pages
                );
                return false;
            }

            protected_pages++;
        }
    }

    *allocation =
        (struct kernel_page_allocation){
            .reservation_base =
                reservation_base,
            .mapped_base = mapped_base,
            .mapped_pages = page_count,
            .guard_pages_before =
                guard_pages_before,
            .guard_pages_after =
                guard_pages_after
        };

    return true;
}

bool kernel_pages_allocate(
    size_t page_count,
    size_t alignment_pages,
    uint64_t flags,
    struct kernel_page_allocation *allocation
)
{
    return kernel_pages_allocate_guarded(
        page_count,
        alignment_pages,
        0,
        0,
        flags,
        allocation
    );
}

bool kernel_pages_release(
    struct kernel_page_allocation *allocation
)
{
    if (allocation == NULL ||
        allocation->mapped_pages == 0) {
        return false;
    }

    size_t reservation_pages;

    if (!add_size(
            allocation->mapped_pages,
            allocation->guard_pages_before,
            &reservation_pages
        ) ||
        !add_size(
            reservation_pages,
            allocation->guard_pages_after,
            &reservation_pages
        ) ||
        reservation_pages >
            UINT64_MAX / KRISHNA_PAGE_SIZE ||
        allocation->reservation_base >
            UINT64_MAX -
                allocation->guard_pages_before *
                    KRISHNA_PAGE_SIZE ||
        allocation->mapped_base !=
            allocation->reservation_base +
                allocation->guard_pages_before *
                    KRISHNA_PAGE_SIZE ||
        allocation->reservation_base >
            UINT64_MAX -
                reservation_pages *
                    KRISHNA_PAGE_SIZE) {
        return false;
    }

    struct vmm_address_space *space =
        vmm_kernel_address_space();

    if (space == NULL) {
        return false;
    }

    /*
     * Guard pages belong to the reservation but must never acquire
     * translations. Refuse to release a damaged descriptor while a
     * mapping still exists outside the owned usable range.
     */
    for (size_t page = 0;
         page < allocation->guard_pages_before;
         page++) {
        uint64_t ignored_physical;

        if (vmm_translate(
                space,
                allocation->reservation_base +
                    page * KRISHNA_PAGE_SIZE,
                &ignored_physical
            )) {
            return false;
        }
    }

    uint64_t trailing_guard_base =
        allocation->mapped_base +
        allocation->mapped_pages *
            KRISHNA_PAGE_SIZE;

    for (size_t page = 0;
         page < allocation->guard_pages_after;
         page++) {
        uint64_t ignored_physical;

        if (vmm_translate(
                space,
                trailing_guard_base +
                    page * KRISHNA_PAGE_SIZE,
                &ignored_physical
            )) {
            return false;
        }
    }

    /*
     * Validate the complete owned mapping before changing anything.
     * This catches stale, forged and double-release descriptors.
     */
    for (size_t page = 0;
         page < allocation->mapped_pages;
         page++) {
        uint64_t physical_address;

        if (!vmm_translate(
                space,
                allocation->mapped_base +
                    page * KRISHNA_PAGE_SIZE,
                &physical_address
            ) ||
            (physical_address &
             (KRISHNA_PAGE_SIZE - 1)) != 0) {
            return false;
        }
    }

    for (size_t page =
             allocation->mapped_pages;
         page > 0;
         page--) {
        uint64_t physical_address;

        if (!vmm_unmap_page(
                space,
                allocation->mapped_base +
                    (page - 1) *
                        KRISHNA_PAGE_SIZE,
                &physical_address
            ) ||
            !pmm_free_page(
                physical_address
            )) {
            return false;
        }
    }

    if (!kernel_vregion_release(
            allocation->reservation_base,
            reservation_pages
        )) {
        return false;
    }

    *allocation =
        (struct kernel_page_allocation){0};

    return true;
}

bool kernel_pages_self_test(void)
{
    struct pmm_statistics before;
    struct pmm_statistics after;

    pmm_get_statistics(&before);

    uint64_t virtual_pages_before =
        kernel_vregion_free_pages();

    struct kernel_page_allocation allocation = {
        .reservation_base = UINT64_C(0x11),
        .mapped_base = UINT64_C(0x22),
        .mapped_pages = 33,
        .guard_pages_before = 44,
        .guard_pages_after = 55
    };

    bool invalid_rejected =
        !kernel_pages_allocate(
            0,
            1,
            VMM_PAGE_WRITABLE |
                VMM_PAGE_NO_EXECUTE,
            &allocation
        ) &&
        allocation.reservation_base ==
            UINT64_C(0x11) &&
        allocation.mapped_base ==
            UINT64_C(0x22) &&
        allocation.mapped_pages == 33 &&
        allocation.guard_pages_before == 44 &&
        allocation.guard_pages_after == 55;

    allocation =
        (struct kernel_page_allocation){0};

    bool allocated =
        kernel_pages_allocate(
            3,
            4,
            VMM_PAGE_WRITABLE |
                VMM_PAGE_NO_EXECUTE,
            &allocation
        );

    bool alignment_passed =
        allocated &&
        (allocation.mapped_base &
         ((4 * KRISHNA_PAGE_SIZE) - 1)) == 0;

    bool translation_passed = allocated;
    struct vmm_address_space *space =
        vmm_kernel_address_space();

    if (allocated && space != NULL) {
        for (size_t page = 0;
             page < allocation.mapped_pages;
             page++) {
            uint64_t physical_address;

            if (!vmm_translate(
                    space,
                    allocation.mapped_base +
                        page * KRISHNA_PAGE_SIZE,
                    &physical_address
                )) {
                translation_passed = false;
                break;
            }
        }
    } else {
        translation_passed = false;
    }

    bool write_read_passed = allocated;

    if (allocated) {
        volatile uint64_t *first =
            (volatile uint64_t *)(uintptr_t)
                allocation.mapped_base;

        volatile uint64_t *last =
            (volatile uint64_t *)(uintptr_t)(
                allocation.mapped_base +
                allocation.mapped_pages *
                    KRISHNA_PAGE_SIZE -
                sizeof(uint64_t)
            );

        *first = UINT64_C(0x4B524953484E4150);
        *last = UINT64_C(0x5452414E53414354);

        write_read_passed =
            *first ==
                UINT64_C(0x4B524953484E4150) &&
            *last ==
                UINT64_C(0x5452414E53414354);
    }

    bool released =
        allocated &&
        kernel_pages_release(&allocation);

    pmm_get_statistics(&after);

    bool cleanup_passed =
        released &&
        allocation.mapped_pages == 0 &&
        before.free_pages ==
            after.free_pages &&
        virtual_pages_before ==
            kernel_vregion_free_pages();

    return invalid_rejected &&
        allocated &&
        alignment_passed &&
        translation_passed &&
        write_read_passed &&
        cleanup_passed;
}
