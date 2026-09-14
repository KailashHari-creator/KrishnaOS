#include "memory/memory_test.h"

#include <stddef.h>

#include "memory/pmm.h"
#include "memory/vmm.h"

#define TEST_VIRTUAL_BASE UINT64_C(0x0000600000000000)
#define TEST_PATTERN_A UINT64_C(0x4B524953484E4121)
#define TEST_PATTERN_B UINT64_C(0x504147494E472121)

bool memory_run_self_test(struct memory_test_result *result)
{
    if (result == NULL) {
        return false;
    }

    *result = (struct memory_test_result){0};

    struct pmm_statistics before;
    struct pmm_statistics after;
    pmm_get_statistics(&before);

    uint64_t page_a = pmm_allocate_page();
    uint64_t page_b = pmm_allocate_page();
    uint64_t block = pmm_allocate_pages(4);

    result->first_page = page_a;
    result->second_page = page_b;
    result->contiguous_block = block;

    result->allocation_passed =
        page_a != PMM_INVALID_ADDRESS &&
        page_b != PMM_INVALID_ADDRESS &&
        block != PMM_INVALID_ADDRESS;

    if (!result->allocation_passed) {
        if (page_a != PMM_INVALID_ADDRESS) {
            pmm_free_page(page_a);
        }
        if (page_b != PMM_INVALID_ADDRESS) {
            pmm_free_page(page_b);
        }
        if (block != PMM_INVALID_ADDRESS) {
            pmm_free_pages(block, 4);
        }
        return false;
    }

    result->distinct_pages_passed =
        page_a != page_b &&
        page_a != block &&
        page_b != block;

    struct vmm_address_space *space = vmm_kernel_address_space();
    bool mapped_a = vmm_map_page(
        space,
        TEST_VIRTUAL_BASE,
        page_a,
        VMM_PAGE_WRITABLE | VMM_PAGE_NO_EXECUTE
    );
    bool mapped_b = vmm_map_page(
        space,
        TEST_VIRTUAL_BASE + VMM_PAGE_SIZE,
        page_b,
        VMM_PAGE_WRITABLE | VMM_PAGE_NO_EXECUTE
    );

    result->mapping_passed = mapped_a && mapped_b;

    if (result->mapping_passed) {
        uint64_t translated_a = 0;
        uint64_t translated_b = 0;

        result->translation_passed =
            vmm_translate(space, TEST_VIRTUAL_BASE, &translated_a) &&
            vmm_translate(
                space,
                TEST_VIRTUAL_BASE + VMM_PAGE_SIZE,
                &translated_b
            ) &&
            translated_a == page_a &&
            translated_b == page_b;

        volatile uint64_t *virtual_a =
            (volatile uint64_t *)TEST_VIRTUAL_BASE;
        volatile uint64_t *virtual_b =
            (volatile uint64_t *)(TEST_VIRTUAL_BASE + VMM_PAGE_SIZE);

        *virtual_a = TEST_PATTERN_A;
        *virtual_b = TEST_PATTERN_B;

        result->write_read_passed =
            *virtual_a == TEST_PATTERN_A &&
            *virtual_b == TEST_PATTERN_B;
    }

    bool unmapped_a = !mapped_a ||
        vmm_unmap_page(space, TEST_VIRTUAL_BASE, NULL);
    bool unmapped_b = !mapped_b ||
        vmm_unmap_page(
            space,
            TEST_VIRTUAL_BASE + VMM_PAGE_SIZE,
            NULL
        );

    bool freed_a = pmm_free_page(page_a);
    bool freed_b = pmm_free_page(page_b);
    bool freed_block = pmm_free_pages(block, 4);

    pmm_get_statistics(&after);

    result->cleanup_passed =
        unmapped_a && unmapped_b &&
        freed_a && freed_b && freed_block &&
        before.free_pages == after.free_pages;

    return result->allocation_passed &&
        result->distinct_pages_passed &&
        result->mapping_passed &&
        result->translation_passed &&
        result->write_read_passed &&
        result->cleanup_passed;
}
