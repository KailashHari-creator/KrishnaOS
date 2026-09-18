#include "user_copy.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/layout.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "task/process.h"
#include "task/thread.h"

static bool user_range(
    const void *buffer,
    size_t size,
    uint64_t *start,
    uint64_t *end
)
{
    if (start == NULL ||
        end == NULL) {
        return false;
    }

    if (size == 0) {
        *start = 0;
        *end = 0;
        return true;
    }

    if (buffer == NULL) {
        return false;
    }

    uint64_t first =
        (uint64_t)(uintptr_t)buffer;

    if (first < USER_SPACE_BASE ||
        (uint64_t)size >
            UINT64_MAX - first) {
        return false;
    }

    uint64_t exclusive_end =
        first + (uint64_t)size;

    if (exclusive_end >
            USER_SPACE_TOP ||
        exclusive_end <= first) {
        return false;
    }

    *start = first;
    *end = exclusive_end;

    return true;
}

static struct vmm_address_space *
current_user_address_space(void)
{
    struct kernel_process *process =
        kernel_thread_current_process();

    if (process == NULL) {
        return NULL;
    }

    return kernel_process_address_space(
        process
    );
}

bool user_buffer_validate(
    const void *user_buffer,
    size_t size,
    bool write_access
)
{
    if (size == 0) {
        return true;
    }

    uint64_t start;
    uint64_t end;

    if (!user_range(
            user_buffer,
            size,
            &start,
            &end
        )) {
        return false;
    }

    struct vmm_address_space *space =
        current_user_address_space();

    if (space == NULL) {
        return false;
    }

    uint64_t current = start;

    while (current < end) {
        uint64_t physical_address;

        if (!vmm_translate_user(
                space,
                current,
                write_access,
                &physical_address
            )) {
            return false;
        }

        uint64_t page_remaining =
            VMM_PAGE_SIZE -
            (current &
             (VMM_PAGE_SIZE -
              UINT64_C(1)));

        uint64_t range_remaining =
            end - current;

        if (range_remaining <=
            page_remaining) {
            return true;
        }

        current += page_remaining;
    }

    return true;
}

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
        !user_buffer_validate(
            user_source,
            size,
            false
        )) {
        return false;
    }

    struct vmm_address_space *space =
        current_user_address_space();

    if (space == NULL) {
        return false;
    }

    uint8_t *destination =
        (uint8_t *)kernel_destination;

    uint64_t current =
        (uint64_t)(uintptr_t)user_source;

    size_t copied = 0;

    while (copied < size) {
        uint64_t physical_address;

        if (!vmm_translate_user(
                space,
                current,
                false,
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
                (VMM_PAGE_SIZE -
                 UINT64_C(1))
            );

        size_t chunk =
            VMM_PAGE_SIZE -
            page_offset;

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

bool copy_to_user(
    void *user_destination,
    const void *kernel_source,
    size_t size
)
{
    if (size == 0) {
        return true;
    }

    if (kernel_source == NULL ||
        !user_buffer_validate(
            user_destination,
            size,
            true
        )) {
        return false;
    }

    struct vmm_address_space *space =
        current_user_address_space();

    if (space == NULL) {
        return false;
    }

    const uint8_t *source =
        (const uint8_t *)kernel_source;

    uint64_t current =
        (uint64_t)(uintptr_t)
            user_destination;

    size_t copied = 0;

    while (copied < size) {
        uint64_t physical_address;

        if (!vmm_translate_user(
                space,
                current,
                true,
                &physical_address
            )) {
            return false;
        }

        uint8_t *destination =
            (uint8_t *)
            pmm_physical_to_virtual(
                physical_address
            );

        if (destination == NULL) {
            return false;
        }

        size_t page_offset =
            (size_t)(
                current &
                (VMM_PAGE_SIZE -
                 UINT64_C(1))
            );

        size_t chunk =
            VMM_PAGE_SIZE -
            page_offset;

        size_t remaining =
            size - copied;

        if (chunk > remaining) {
            chunk = remaining;
        }

        for (size_t index = 0;
             index < chunk;
             index++) {
            destination[index] =
                source[copied + index];
        }

        copied += chunk;
        current += chunk;
    }

    return true;
}