#include "syscall.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/context_switch.h"
#include "interrupts.h"
#include "sync/spinlock.h"
#include "task/thread.h"

extern void arch_syscall_interrupt_entry(void);

static uint64_t completed_exit_count;
static uint64_t last_exit_status = UINT64_MAX;
static uint64_t last_exit_process_id = UINT64_MAX;
static bool syscall_initialized;

bool syscall_init(void)
{
    if (syscall_initialized) {
        return false;
    }

    interrupt_state_t interrupt_state =
        interrupt_save_disable();

    bool installed =
        interrupts_install_user_gate(
            KRISHNA_SYSCALL_VECTOR,
            (uintptr_t)arch_syscall_interrupt_entry
        );

    interrupt_restore(interrupt_state);

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
        (struct arch_interrupt_context *)(void *)
            interrupted_rsp;

    switch (context->rax) {
        case KRISHNA_SYSCALL_EXIT:
            /*
             * System V argument register RDI contains the exit
             * status supplied by userspace.
             */
            __atomic_store_n(
                &last_exit_status,
                context->rdi,
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

        default:
            /*
             * Unknown syscall.
             */
            context->rax = UINT64_MAX;
            return interrupted_rsp;
    }
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