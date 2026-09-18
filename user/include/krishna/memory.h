#ifndef KRISHNA_USER_MEMORY_H
#define KRISHNA_USER_MEMORY_H

#include <stddef.h>
#include <stdint.h>

int64_t krishna_memory_map(
    uint64_t handle,
    void *requested_address,
    size_t length,
    uint64_t offset,
    uint64_t protection,
    uint64_t flags
);

int64_t krishna_memory_unmap(
    uint64_t handle,
    void *address,
    size_t length
);

#endif