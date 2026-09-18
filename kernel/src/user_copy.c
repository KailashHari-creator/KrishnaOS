#include "user_copy.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/layout.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "task/process.h"
#include "task/thread.h"

bool copy_from_user(
    void *kernel_destination,
    const void *user_source,
    size_t size
)
{
    if (size == 0) {
        return true;
    }

    if (kernel_destination == NULL ||
        user_source == NULL) {
        return false;
    }

    uint64_t start =
        (uint64_t)(uintptr_t)user_source;

    /*
     * Check addition before calculating the exclusive end address.
     */
    if (start < USER_SPACE_BASE ||
        (uint64_t)size > UINT64_MAX - start) {
        return false;
    }

    uint64_t end =
        start + (uint64_t)size;

    /*
     * USER_SPACE_TOP is treated as an exclusive upper boundary by the
     * ELF loader and userspace stack arrangement.
     */
    if (end > USER_SPACE_TOP ||
        end <= start) {
        return false;
    }

    struct kernel_process *process =
        kernel_thread_current_process();

    if (process == NULL) {
        return false;
    }

    struct vmm_address_space *space =
        kernel_process_address_space(
            process
        );

    if (space == NULL) {
        return false;
    }

    uint8_t *destination =
        (uint8_t *)kernel_destination;

    uint64_t current = start;
    size_t copied = 0;

    while (copied < size) {
        uint64_t physical_address;

        if (!vmm_translate(
                space,
                current,
                &physical_address
            )) {
            return false;
        }

        const uint8_t *source =
            (const uint8_t *)
            pmm_physical_to_virtual(
                physical_address
            );

        if (source == NULL) {
            return false;
        }

        size_t page_offset =
            (size_t)(
                current &
                (VMM_PAGE_SIZE - UINT64_C(1))
            );

        size_t chunk =
            VMM_PAGE_SIZE - page_offset;

        size_t remaining =
            size - copied;

        if (chunk > remaining) {
            chunk = remaining;
        }

        for (size_t index = 0;
             index < chunk;
             index++) {
            destination[copied + index] =
                source[index];
        }

        copied += chunk;
        current += chunk;
    }

    return true;
}