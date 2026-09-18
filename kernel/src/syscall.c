#include "syscall.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/context_switch.h"
#include "interrupts.h"
#include "task/thread.h"
#include "user_copy.h"
#include "object/object.h"
#include "object/serial_console.h"
#include "object/input.h"
#include "task/process.h"

extern void arch_syscall_interrupt_entry(void);

#define SYSCALL_WRITE_BUFFER_SIZE ((size_t)128)
#define SYSCALL_MAX_WRITE_SIZE    ((size_t)4096)
#define SYSCALL_READ_BUFFER_SIZE ((size_t)128)
#define SYSCALL_MAX_READ_SIZE    ((size_t)4096)

typedef int64_t (*syscall_handler_t)(
    uint64_t argument_1,
    uint64_t argument_2,
    uint64_t argument_3,
    uint64_t argument_4,
    uint64_t argument_5,
    uint64_t argument_6
);

static uint64_t completed_exit_count;
static uint64_t last_exit_status =
    UINT64_MAX;

static uint64_t last_exit_process_id =
    UINT64_MAX;

static bool syscall_initialized;


static _Noreturn int64_t syscall_handle_exit(
    uint64_t status,
    uint64_t ignored_2,
    uint64_t ignored_3,
    uint64_t ignored_4,
    uint64_t ignored_5,
    uint64_t ignored_6
)
{
    (void)ignored_2;
    (void)ignored_3;
    (void)ignored_4;
    (void)ignored_5;
    (void)ignored_6;

    __atomic_store_n(
        &last_exit_status,
        status,
        __ATOMIC_RELEASE
    );

    __atomic_store_n(
        &last_exit_process_id,
        kernel_thread_current_process_id(),
        __ATOMIC_RELEASE
    );

    __atomic_add_fetch(
        &completed_exit_count,
        UINT64_C(1),
        __ATOMIC_RELEASE
    );

    kernel_thread_exit();
}

static int64_t syscall_handle_read(
    uint64_t handle,
    uint64_t user_buffer,
    uint64_t size,
    uint64_t ignored_4,
    uint64_t ignored_5,
    uint64_t ignored_6
)
{
    (void)ignored_4;
    (void)ignored_5;
    (void)ignored_6;

    struct kernel_process *process =
        kernel_thread_current_process();

    if (process == NULL) {
        return -KRISHNA_ERROR_NO_SUCH_PROCESS;
    }

    struct kernel_object *object =
        kernel_process_handle_acquire(
            process,
            handle,
            KERNEL_HANDLE_RIGHT_READ
        );

    if (object == NULL) {
        return -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR;
    }

    if (size == 0) {
        kernel_object_release(object);
        return 0;
    }

    if (size > SYSCALL_MAX_READ_SIZE) {
        kernel_object_release(object);
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    /*
     * Validate the complete destination before consuming an input
     * event. Otherwise an invalid pointer would discard the event.
     */
    if (!user_buffer_validate(
            (void *)(uintptr_t)user_buffer,
            (size_t)size,
            true
        )) {
        kernel_object_release(object);
        return -KRISHNA_ERROR_ACCESS_FAULT;
    }

    uint8_t buffer[SYSCALL_READ_BUFFER_SIZE];
    size_t total = 0;

    while (total < (size_t)size) {
        size_t chunk =
            (size_t)size - total;

        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }

        int64_t result =
            kernel_object_read(
                object,
                buffer,
                chunk
            );

        if (result < 0) {
            kernel_object_release(object);

            return total != 0
                ? (int64_t)total
                : result;
        }

        if ((uint64_t)result >
            (uint64_t)chunk) {
            kernel_object_release(object);
            return -KRISHNA_ERROR_IO;
        }

        if (result == 0) {
            break;
        }

        if (user_buffer >
            UINT64_MAX - total ||
            !copy_to_user(
                (void *)(uintptr_t)(
                    user_buffer + total
                ),
                buffer,
                (size_t)result
            )) {
            kernel_object_release(object);

            return total != 0
                ? (int64_t)total
                : -KRISHNA_ERROR_ACCESS_FAULT;
        }

        total += (size_t)result;

        if ((size_t)result < chunk) {
            break;
        }
    }

    kernel_object_release(object);
    return (int64_t)total;
}

static int64_t syscall_handle_write(
    uint64_t handle,
    uint64_t user_buffer,
    uint64_t size,
    uint64_t ignored_4,
    uint64_t ignored_5,
    uint64_t ignored_6
)
{
    (void)ignored_4;
    (void)ignored_5;
    (void)ignored_6;

    struct kernel_process *process =
        kernel_thread_current_process();

    if (process == NULL) {
        return -KRISHNA_ERROR_NO_SUCH_PROCESS;
    }

    struct kernel_object *object =
        kernel_process_handle_acquire(
            process,
            handle,
            KERNEL_HANDLE_RIGHT_WRITE
        );

    if (object == NULL) {
        return -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR;
    }

    if (size == 0) {
        kernel_object_release(object);
        return 0;
    }

    if (size > SYSCALL_MAX_WRITE_SIZE) {
        kernel_object_release(object);
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    char buffer[SYSCALL_WRITE_BUFFER_SIZE];
    size_t written = 0;

    while (written < (size_t)size) {
        size_t chunk =
            (size_t)size - written;

        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }

        if (user_buffer >
            UINT64_MAX - written) {
            kernel_object_release(object);
            return -KRISHNA_ERROR_ACCESS_FAULT;
        }

        uint64_t chunk_address =
            user_buffer + written;

        if (!copy_from_user(
                buffer,
                (const void *)(uintptr_t)
                    chunk_address,
                chunk
            )) {
            kernel_object_release(object);

            return written != 0
                ? (int64_t)written
                : -KRISHNA_ERROR_ACCESS_FAULT;
        }

        int64_t result =
            kernel_object_write(
                object,
                buffer,
                chunk
            );

        if (result < 0) {
            kernel_object_release(object);

            return written != 0
                ? (int64_t)written
                : result;
        }

        if ((uint64_t)result >
            (uint64_t)chunk) {
            kernel_object_release(object);
            return -KRISHNA_ERROR_IO;
        }

        if (result == 0) {
            break;
        }

        written += (size_t)result;

        if ((size_t)result < chunk) {
            break;
        }
    }

    kernel_object_release(object);
    return (int64_t)written;
}

static int64_t syscall_handle_close(
    uint64_t handle,
    uint64_t ignored_2,
    uint64_t ignored_3,
    uint64_t ignored_4,
    uint64_t ignored_5,
    uint64_t ignored_6
)
{
    (void)ignored_2;
    (void)ignored_3;
    (void)ignored_4;
    (void)ignored_5;
    (void)ignored_6;

    struct kernel_process *process =
        kernel_thread_current_process();

    if (process == NULL) {
        return -KRISHNA_ERROR_NO_SUCH_PROCESS;
    }

    if (!kernel_process_handle_close(
            process,
            handle
        )) {
        return -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR;
    }

    return 0;
}

static int64_t syscall_handle_getpid(
    uint64_t ignored_1,
    uint64_t ignored_2,
    uint64_t ignored_3,
    uint64_t ignored_4,
    uint64_t ignored_5,
    uint64_t ignored_6
)
{
    (void)ignored_1;
    (void)ignored_2;
    (void)ignored_3;
    (void)ignored_4;
    (void)ignored_5;
    (void)ignored_6;

    uint64_t process_id =
        kernel_thread_current_process_id();

    if (process_id == UINT64_MAX ||
        process_id > INT64_MAX) {
        return -KRISHNA_ERROR_NO_SUCH_PROCESS;
    }

    return (int64_t)process_id;
}


static int64_t syscall_handle_yield(
    uint64_t ignored_1,
    uint64_t ignored_2,
    uint64_t ignored_3,
    uint64_t ignored_4,
    uint64_t ignored_5,
    uint64_t ignored_6
)
{
    (void)ignored_1;
    (void)ignored_2;
    (void)ignored_3;
    (void)ignored_4;
    (void)ignored_5;
    (void)ignored_6;

    kernel_thread_yield();

    return 0;
}


/*
 * Unimplemented entries remain NULL and produce -ENOSYS.
 */
static const syscall_handler_t syscall_table[
    KRISHNA_SYSCALL_COUNT
] = {
    [KRISHNA_SYSCALL_EXIT] =
        syscall_handle_exit,

    [KRISHNA_SYSCALL_READ] =
        syscall_handle_read,

    [KRISHNA_SYSCALL_WRITE] =
        syscall_handle_write,

    [KRISHNA_SYSCALL_GETPID] =
        syscall_handle_getpid,

    [KRISHNA_SYSCALL_YIELD] =
        syscall_handle_yield,
    
    [KRISHNA_SYSCALL_CLOSE] =
        syscall_handle_close
};

static uint64_t syscall_interrupt_save_disable(void)
{
    uint64_t flags;

    __asm__ volatile(
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );

    return flags;
}

static void syscall_interrupt_restore(uint64_t flags)
{
    /*
     * Bit 9 of RFLAGS is IF. Re-enable interrupts only if they
     * were enabled before syscall initialization began.
     */
    if ((flags & (UINT64_C(1) << 9)) != 0) {
        __asm__ volatile(
            "sti"
            :
            :
            : "memory"
        );
    }
}

bool syscall_init(void)
{
    if (syscall_initialized) {
        return false;
    }

    if (!serial_console_object_init()) {
        return false;
    }

    if (!input_objects_init()) {
        return false;
    }

    uint64_t interrupt_flags =
        syscall_interrupt_save_disable();

    bool installed =
        interrupts_install_user_gate(
            KRISHNA_SYSCALL_VECTOR,
            (uintptr_t)arch_syscall_interrupt_entry
        );

    syscall_interrupt_restore(interrupt_flags);

    if (!installed) {
        return false;
    }

    completed_exit_count = 0;
    last_exit_status = UINT64_MAX;
    last_exit_process_id = UINT64_MAX;
    syscall_initialized = true;

    return true;
}


uint64_t *syscall_interrupt_dispatch(
    uint64_t *interrupted_rsp
)
{
    if (!syscall_initialized ||
        interrupted_rsp == NULL) {
        return interrupted_rsp;
    }

    struct arch_interrupt_context *context =
        (struct arch_interrupt_context *)
        (void *)interrupted_rsp;

    uint64_t syscall_number =
        context->rax;

    int64_t result =
        -KRISHNA_ERROR_NOT_IMPLEMENTED;

    if (syscall_number <
            KRISHNA_SYSCALL_COUNT &&
        syscall_table[syscall_number] !=
            NULL) {
        result =
            syscall_table[syscall_number](
                context->rdi,
                context->rsi,
                context->rdx,
                context->r10,
                context->r8,
                context->r9
            );
    }

    /*
     * Negative signed error values are returned as their ordinary
     * two's-complement RAX representations.
     */
    context->rax =
        (uint64_t)result;

    return interrupted_rsp;
}


uint64_t syscall_exit_count(void)
{
    return __atomic_load_n(
        &completed_exit_count,
        __ATOMIC_ACQUIRE
    );
}


uint64_t syscall_last_exit_status(void)
{
    return __atomic_load_n(
        &last_exit_status,
        __ATOMIC_ACQUIRE
    );
}


uint64_t syscall_last_exit_process_id(void)
{
    return __atomic_load_n(
        &last_exit_process_id,
        __ATOMIC_ACQUIRE
    );
}