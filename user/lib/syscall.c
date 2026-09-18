#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>
#include <krishna/io.h>
#include <krishna/process.h>
#include <krishna/syscall.h>

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