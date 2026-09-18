#ifndef KRISHNA_USER_DEVICE_H
#define KRISHNA_USER_DEVICE_H

#include <stddef.h>
#include <stdint.h>

int64_t krishna_ioctl(
    uint64_t handle,
    uint64_t request,
    void *buffer,
    size_t size
);

#endif