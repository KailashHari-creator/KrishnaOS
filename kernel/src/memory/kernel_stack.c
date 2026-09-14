#include "memory/kernel_stack.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/layout.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/vregion.h"

bool kernel_stack_allocate(
    size_t usable_pages,
    struct kernel_stack *stack
)
{
    if (stack == NULL ||
        usable_pages == 0 ||
        usable_pages >
            UINT64_MAX / KRISHNA_PAGE_SIZE) {
        return false;
    }

    struct kernel_page_allocation allocation;

    if (!kernel_pages_allocate_guarded(
            usable_pages,
            1,
            1,
            1,
            VMM_PAGE_WRITABLE |
                VMM_PAGE_NO_EXECUTE,
            &allocation
        )) {
        return false;
    }

    uint64_t stack_top =
        allocation.mapped_base +
        usable_pages * KRISHNA_PAGE_SIZE;

    *stack = (struct kernel_stack){
        .allocation = allocation,
        .stack_bottom =
            allocation.mapped_base,
        .stack_top = stack_top
    };

    return true;
}

bool kernel_stack_release(
    struct kernel_stack *stack
)
{
    if (stack == NULL ||
        stack->allocation.mapped_pages == 0 ||
        stack->allocation.mapped_pages >
            UINT64_MAX / KRISHNA_PAGE_SIZE ||
        stack->stack_bottom >
            UINT64_MAX -
                stack->allocation.mapped_pages *
                    KRISHNA_PAGE_SIZE ||
        stack->stack_bottom !=
            stack->allocation.mapped_base ||
        stack->stack_top !=
            stack->stack_bottom +
                stack->allocation.mapped_pages *
                    KRISHNA_PAGE_SIZE) {
        return false;
    }

    struct kernel_page_allocation allocation =
        stack->allocation;

    if (!kernel_pages_release(&allocation)) {
        return false;
    }

    *stack = (struct kernel_stack){0};
    return true;
}

bool kernel_stack_self_test(void)
{
    struct pmm_statistics before;
    struct pmm_statistics after;

    pmm_get_statistics(&before);

    uint64_t virtual_pages_before =
        kernel_vregion_free_pages();

    struct kernel_stack stack = {0};

    bool allocated =
        kernel_stack_allocate(4, &stack);

    bool layout_passed =
        allocated &&
        stack.allocation.guard_pages_before == 1 &&
        stack.allocation.guard_pages_after == 1 &&
        stack.stack_bottom ==
            stack.allocation.reservation_base +
                KRISHNA_PAGE_SIZE &&
        stack.stack_top ==
            stack.stack_bottom +
                4 * KRISHNA_PAGE_SIZE &&
        (stack.stack_top & UINT64_C(0xF)) == 0;

    struct vmm_address_space *space =
        vmm_kernel_address_space();

    uint64_t ignored_physical;
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

    bool usable_mapped =
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

    bool write_read_passed = allocated;

    if (allocated) {
        volatile uint64_t *bottom =
            (volatile uint64_t *)(uintptr_t)
                stack.stack_bottom;

        volatile uint64_t *top =
            (volatile uint64_t *)(uintptr_t)(
                stack.stack_top -
                sizeof(uint64_t)
            );

        *bottom = UINT64_C(0x4B535441434B4C4F);
        *top = UINT64_C(0x4B535441434B4849);

        write_read_passed =
            *bottom ==
                UINT64_C(0x4B535441434B4C4F) &&
            *top ==
                UINT64_C(0x4B535441434B4849);
    }

    bool released =
        allocated &&
        kernel_stack_release(&stack);

    pmm_get_statistics(&after);

    bool cleanup_passed =
        released &&
        stack.stack_bottom == 0 &&
        stack.stack_top == 0 &&
        before.free_pages ==
            after.free_pages &&
        virtual_pages_before ==
            kernel_vregion_free_pages();

    return allocated &&
        layout_passed &&
        guards_unmapped &&
        usable_mapped &&
        write_read_passed &&
        cleanup_passed;
}
