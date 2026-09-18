#ifndef KRISHNA_USER_COPY_H
#define KRISHNA_USER_COPY_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Copy bytes from the current process into kernel memory.
 *
 * Every userspace page is translated independently. Kernel addresses,
 * unmapped addresses and overflowing ranges are rejected.
 */
bool copy_from_user(
    void *kernel_destination,
    const void *user_source,
    size_t size
);

#endif