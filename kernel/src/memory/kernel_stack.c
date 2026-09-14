#include "memory/kernel_stack.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/layout.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/vregion.h"


#define KERNEL_STACK_GUARD_PAGES 1


bool kernel_stack_allocate(
    size_t usable_pages,
    struct kernel_stack *stack
)
{
    if (stack == NULL ||
        usable_pages == 0 ||
        usable_pages >
            UINT64_MAX /
                KRISHNA_PAGE_SIZE) {
        return false;
    }

    /*
     * Build the allocation locally so the caller's descriptor
     * remains unchanged if any part of allocation fails.
     */
    struct kernel_page_allocation allocation;

    if (!kernel_pages_allocate_guarded(
            usable_pages,
            1,
            KERNEL_STACK_GUARD_PAGES,
            KERNEL_STACK_GUARD_PAGES,
            VMM_PAGE_WRITABLE |
                VMM_PAGE_NO_EXECUTE,
            &allocation
        )) {
        return false;
    }

    uint64_t stack_bytes =
        usable_pages *
            KRISHNA_PAGE_SIZE;

    if (allocation.mapped_base >
        UINT64_MAX - stack_bytes) {
        /*
         * This should already be prevented by the virtual-region
         * allocator, but reject it defensively.
         */
        (void)kernel_pages_release(
            &allocation
        );

        return false;
    }

    uint64_t stack_top =
        allocation.mapped_base +
        stack_bytes;

    /*
     * Publish only after the entire guarded stack is ready.
     */
    *stack = (struct kernel_stack){
        .allocation = allocation,

        .stack_bottom =
            allocation.mapped_base,

        .stack_top =
            stack_top
    };

    return true;
}


bool kernel_stack_release(
    struct kernel_stack *stack
)
{
    if (stack == NULL ||
        stack->allocation.mapped_pages == 0) {
        return false;
    }

    if (stack->allocation.mapped_pages >
        UINT64_MAX /
            KRISHNA_PAGE_SIZE) {
        return false;
    }

    uint64_t stack_bytes =
        stack->allocation.mapped_pages *
            KRISHNA_PAGE_SIZE;

    if (stack->stack_bottom >
        UINT64_MAX - stack_bytes) {
        return false;
    }

    /*
     * Reject forged or damaged stack metadata before releasing any
     * page. The cached stack bounds must agree with the underlying
     * kernel-page allocation.
     */
    if (stack->stack_bottom !=
            stack->allocation.mapped_base ||
        stack->stack_top !=
            stack->stack_bottom +
                stack_bytes ||
        stack->allocation.guard_pages_before !=
            KERNEL_STACK_GUARD_PAGES ||
        stack->allocation.guard_pages_after !=
            KERNEL_STACK_GUARD_PAGES) {
        return false;
    }

    /*
     * kernel_pages_release() clears its descriptor. Use a local copy
     * so the complete stack object changes only after success.
     */
    struct kernel_page_allocation allocation =
        stack->allocation;

    if (!kernel_pages_release(
            &allocation
        )) {
        return false;
    }

    *stack =
        (struct kernel_stack){0};

    return true;
}


bool kernel_stack_self_test(void)
{
    struct pmm_statistics before;
    struct pmm_statistics after;

    pmm_get_statistics(&before);

    uint64_t virtual_pages_before =
        kernel_vregion_free_pages();

    struct kernel_stack stack =
        (struct kernel_stack){0};

    /*
     * Four usable pages plus one guard on each side:
     *
     *     1 + 4 + 1 = 6 reserved virtual pages
     */
    bool allocated =
        kernel_stack_allocate(
            4,
            &stack
        );

    bool layout_passed =
        allocated &&
        stack.allocation.mapped_pages == 4 &&
        stack.allocation.guard_pages_before ==
            KERNEL_STACK_GUARD_PAGES &&
        stack.allocation.guard_pages_after ==
            KERNEL_STACK_GUARD_PAGES &&
        stack.stack_bottom ==
            stack.allocation.reservation_base +
                KRISHNA_PAGE_SIZE &&
        stack.stack_top ==
            stack.stack_bottom +
                4 * KRISHNA_PAGE_SIZE &&
        /*
         * A page boundary is naturally suitable for the required
         * 16-byte x86-64 stack alignment.
         */
        (stack.stack_top &
         UINT64_C(0xF)) == 0;

    struct vmm_address_space *space =
        vmm_kernel_address_space();

    uint64_t ignored_physical;

    /*
     * Do not read or write guard pages directly during the ordinary
     * boot test: doing so would intentionally halt through the page
     * fault handler. Translation inspection proves they are absent.
     */
    bool guards_unmapped =
        allocated &&
        space != NULL &&
        !vmm_translate(
            space,
            stack.allocation.reservation_base,
            &ignored_physical
        ) &&
        !vmm_translate(
            space,
            stack.stack_top,
            &ignored_physical
        );

    /*
     * The first and last bytes of the usable stack must translate.
     */
    bool usable_range_mapped =
        allocated &&
        space != NULL &&
        vmm_translate(
            space,
            stack.stack_bottom,
            &ignored_physical
        ) &&
        vmm_translate(
            space,
            stack.stack_top - 1,
            &ignored_physical
        );

    /*
     * kernel_pages_allocate_guarded() zero-fills the mapped range.
     */
    bool zeroing_passed =
        allocated;

    if (allocated) {
        const uint8_t *bytes =
            (const uint8_t *)(uintptr_t)
                stack.stack_bottom;

        size_t stack_bytes =
            stack.allocation.mapped_pages *
                KRISHNA_PAGE_SIZE;

        for (size_t index = 0;
             index < stack_bytes;
             index++) {
            if (bytes[index] != 0) {
                zeroing_passed = false;
                break;
            }
        }
    }

    /*
     * Verify both usable boundaries without touching either guard.
     */
    bool write_read_passed =
        allocated;

    if (allocated) {
        volatile uint64_t *bottom =
            (volatile uint64_t *)(uintptr_t)
                stack.stack_bottom;

        volatile uint64_t *top =
            (volatile uint64_t *)(uintptr_t)(
                stack.stack_top -
                sizeof(uint64_t)
            );

        *bottom =
            UINT64_C(0x4B535441434B4C4F);

        *top =
            UINT64_C(0x4B535441434B4849);

        write_read_passed =
            *bottom ==
                UINT64_C(
                    0x4B535441434B4C4F
                ) &&
            *top ==
                UINT64_C(
                    0x4B535441434B4849
                );
    }

    bool released =
        allocated &&
        kernel_stack_release(
            &stack
        );

    pmm_get_statistics(&after);

    bool cleanup_passed =
        released &&
        stack.allocation.mapped_pages == 0 &&
        stack.stack_bottom == 0 &&
        stack.stack_top == 0 &&
        before.free_pages ==
            after.free_pages &&
        virtual_pages_before ==
            kernel_vregion_free_pages();

    return allocated &&
        layout_passed &&
        guards_unmapped &&
        usable_range_mapped &&
        zeroing_passed &&
        write_read_passed &&
        cleanup_passed;
}