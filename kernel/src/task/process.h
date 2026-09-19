#ifndef KRISHNA_TASK_PROCESS_H
#define KRISHNA_TASK_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/vmm.h"

enum kernel_process_state {
    KERNEL_PROCESS_ALIVE = 0,
    KERNEL_PROCESS_TERMINATED
};

struct kernel_process;

/*
 * Register PID 0 as the kernel process.
 *
 * Must be called after the kernel heap and KRISHNA-owned page tables
 * have been initialized.
 */
bool kernel_process_system_init(void);

/*
 * Create a process with an empty private userspace address space.
 */
struct kernel_process *kernel_process_create(void);

/*
 * Destroy a process that has no threads and no remaining userspace
 * mappings.
 */
bool kernel_process_destroy(
    struct kernel_process *process
);

struct kernel_process *kernel_process_kernel(void);

uint64_t kernel_process_id(
    const struct kernel_process *process
);

enum kernel_process_state kernel_process_state(
    const struct kernel_process *process
);

struct vmm_address_space *kernel_process_address_space(
    struct kernel_process *process
);

size_t kernel_process_thread_count(
    const struct kernel_process *process
);

/*
 * Hold/release one thread reference to a process.
 *
 * A process cannot be destroyed while its thread count is nonzero.
 */
bool kernel_process_attach_thread(
    struct kernel_process *process
);

bool kernel_process_detach_thread(
    struct kernel_process *process
);

struct kernel_object;

#define KERNEL_PROCESS_MAX_HANDLES \
    ((size_t)64)

enum kernel_handle_rights {
    KERNEL_HANDLE_RIGHT_READ =
        UINT32_C(1) << 0,

    KERNEL_HANDLE_RIGHT_WRITE =
        UINT32_C(1) << 1,

    KERNEL_HANDLE_RIGHT_IOCTL =
        UINT32_C(1) << 2,

    KERNEL_HANDLE_RIGHT_POLL =
        UINT32_C(1) << 3,

    KERNEL_HANDLE_RIGHT_MAP =
        UINT32_C(1) << 4
};

bool kernel_process_handle_install(
    struct kernel_process *process,
    uint64_t handle,
    struct kernel_object *object,
    uint32_t rights
);

/*
 * Returns a retained object. The caller must call
 * kernel_object_release() after using it.
 */
struct kernel_object *kernel_process_handle_acquire(
    struct kernel_process *process,
    uint64_t handle,
    uint32_t required_rights
);

bool kernel_process_handle_close(
    struct kernel_process *process,
    uint64_t handle
);

#define KERNEL_PROCESS_FIRST_DYNAMIC_HANDLE \
    ((uint64_t)6)

bool kernel_process_handle_allocate(
    struct kernel_process *process,
    struct kernel_object *object,
    uint32_t rights,
    uint64_t *result_handle
);

bool kernel_process_handle_duplicate(
    struct kernel_process *source_process,
    uint64_t source_handle,
    struct kernel_process *target_process,
    uint64_t target_handle,
    uint32_t rights
);

/*
 * Verify creation, kernel sharing, userspace isolation, CR3
 * switching and complete cleanup.
 */
bool kernel_process_self_test(void);

#endif