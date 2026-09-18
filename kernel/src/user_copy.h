#ifndef KRISHNA_USER_COPY_H
#define KRISHNA_USER_COPY_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Validate a userspace range without copying it.
 */
bool user_buffer_validate(
    const void *user_buffer,
    size_t size,
    bool write_access
);

bool copy_from_user(
    void *kernel_destination,
    const void *user_source,
    size_t size
);

bool copy_to_user(
    void *user_destination,
    const void *kernel_source,
    size_t size
);

#endif