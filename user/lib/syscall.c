#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>
#include <krishna/io.h>
#include <krishna/process.h>
#include <krishna/syscall.h>
#include <krishna/device.h>
#include <krishna/memory.h>
#include <krishna/ipc.h>

int64_t krishna_write(
    uint64_t handle,
    const void *buffer,
    size_t size
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_WRITE,
        handle,
        (uint64_t)(uintptr_t)buffer,
        (uint64_t)size,
        0,
        0,
        0
    );
}

int64_t krishna_sleep(
    uint64_t milliseconds
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_SLEEP,
        milliseconds,
        0,
        0,
        0,
        0,
        0
    );
}

_Noreturn void krishna_exit(
    int64_t status
)
{
    (void)krishna_syscall6(
        KRISHNA_SYSCALL_EXIT,
        (uint64_t)status,
        0,
        0,
        0,
        0,
        0
    );

    /*
     * SYS_EXIT must never return.
     */
    for (;;) {
        __asm__ volatile ("ud2");
    }
}


int64_t krishna_getpid(void)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_GETPID,
        0,
        0,
        0,
        0,
        0,
        0
    );
}


int64_t krishna_yield(void)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_YIELD,
        0,
        0,
        0,
        0,
        0,
        0
    );
}

int64_t krishna_close(
    uint64_t handle
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_CLOSE,
        handle,
        0,
        0,
        0,
        0,
        0
    );
}

int64_t krishna_read(
    uint64_t handle,
    void *buffer,
    size_t size
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_READ,
        handle,
        (uint64_t)(uintptr_t)buffer,
        (uint64_t)size,
        0,
        0,
        0
    );
}

int64_t krishna_ioctl(
    uint64_t handle,
    uint64_t request,
    void *buffer,
    size_t size
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_IOCTL,
        handle,
        request,
        (uint64_t)(uintptr_t)buffer,
        (uint64_t)size,
        0,
        0
    );
}

int64_t krishna_memory_map(
    uint64_t handle,
    void *requested_address,
    size_t length,
    uint64_t offset,
    uint64_t protection,
    uint64_t flags
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_MEMORY_MAP,
        handle,
        (uint64_t)(uintptr_t)
            requested_address,
        (uint64_t)length,
        offset,
        protection,
        flags
    );
}

int64_t krishna_memory_unmap(
    uint64_t handle,
    void *address,
    size_t length
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_MEMORY_UNMAP,
        handle,
        (uint64_t)(uintptr_t)address,
        (uint64_t)length,
        0,
        0,
        0
    );
}

int64_t krishna_process_spawn(
    const char *path,
    size_t path_length,
    uint64_t inherited_handle,
    uint64_t child_handle
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_PROCESS_SPAWN,
        (uint64_t)(uintptr_t)path,
        path_length,
        inherited_handle,
        child_handle,
        0,
        0
    );
}

int64_t krishna_channel_create(
    struct krishna_channel_pair *pair
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_CHANNEL_CREATE,
        (uint64_t)(uintptr_t)pair,
        0, 0, 0, 0, 0
    );
}

int64_t krishna_channel_send(
    uint64_t handle,
    const void *message,
    size_t size
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_CHANNEL_SEND,
        handle,
        (uint64_t)(uintptr_t)message,
        size,
        0, 0, 0
    );
}

int64_t krishna_channel_receive(
    uint64_t handle,
    void *message,
    size_t capacity
)
{
    return krishna_syscall6(
        KRISHNA_SYSCALL_CHANNEL_RECEIVE,
        handle,
        (uint64_t)(uintptr_t)message,
        capacity,
        0, 0, 0
    );
}