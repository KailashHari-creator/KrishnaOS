#ifndef KRISHNA_USER_IO_H
#define KRISHNA_USER_IO_H

#include <stddef.h>
#include <stdint.h>

int64_t krishna_write(
    uint64_t handle,
    const void *buffer,
    size_t size
);

int64_t krishna_close(
    uint64_t handle
);

#endif