#ifndef KRISHNA_USER_IPC_H
#define KRISHNA_USER_IPC_H

#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>

int64_t krishna_channel_create(
    struct krishna_channel_pair *pair
);

int64_t krishna_channel_send(
    uint64_t handle,
    const void *message,
    size_t size
);

int64_t krishna_channel_receive(
    uint64_t handle,
    void *message,
    size_t capacity
);

#endif