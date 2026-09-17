#ifndef KRISHNA_TASK_THREAD_H
#define KRISHNA_TASK_THREAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef void (*kernel_thread_entry_t)(
    void *argument
);


enum kernel_thread_state {
    KERNEL_THREAD_READY =0,
    KERNEL_THREAD_RUNNING,
    KERNEL_THREAD_BLOCKED,
    KERNEL_THREAD_TERMINATED
};


struct kernel_thread;


/*
 * Register the currently executing boot context as thread zero.
 */
bool kernel_thread_system_init(void);


/*
 * Create a kernel thread with its own guarded stack.
 *
 * The thread becomes runnable but does not execute until the current
 * thread yields.
 */
struct kernel_thread *kernel_thread_create(
    kernel_thread_entry_t entry,
    void *argument,
    size_t stack_pages
);


/*
 * Voluntarily allow another ready kernel thread to execute.
 */
void kernel_thread_yield(void);


/*
 * Terminate the currently executing dynamic kernel thread.
 *
 * The boot thread cannot terminate.
 */
_Noreturn void kernel_thread_exit(void);


/*
 * Return the current thread's identifier.
 */
uint64_t kernel_thread_current_id(void);


/*
 * Number of live dynamically allocated threads.
 *
 * The boot thread is not included.
 */
uint64_t kernel_thread_live_count(void);


/*
 * Create two threads and verify independent guarded-stack execution,
 * round-robin switching, termination and cleanup.
 */
bool kernel_thread_self_test(void);


bool kernel_thread_block(void);
bool kernel_thread_wake(struct kernel_thread *thread);

bool kernel_thread_blocking_self_test(void);

/*
 * Called from the assembly-backed Local APIC interrupt path.
 */
uint64_t *kernel_thread_timer_interrupt(
    uint64_t *interrupted_rsp
);


/*
 * Called from the assembly-backed voluntary reschedule interrupt.
 */
uint64_t *kernel_thread_reschedule_interrupt(
    uint64_t *interrupted_rsp
);

/*
 * Safe non-interrupt maintenance point for deferred zombie cleanup.
 */
void kernel_thread_preemption_point(void);


/*
 * Return the number of scheduler timer ticks observed.
 */
uint64_t kernel_thread_scheduler_ticks(void);

/*
 * Verify forced timer-driven round-robin scheduling.
 */
bool kernel_thread_timer_self_test(void);

/*
 * Enable forced timer-driven thread scheduling.
 *
 * Call only after the Local APIC timer has been initialized and
 * validated.
 */
bool kernel_thread_enable_preemption(void);

#endif