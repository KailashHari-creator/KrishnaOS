#ifndef KRISHNA_SYNC_SPINLOCK_H
#define KRISHNA_SYNC_SPINLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t interrupt_state_t;

typedef struct spinlock {
    uint32_t locked;
} spinlock_t;

#define SPINLOCK_INITIALIZER \
    { .locked = 0U }


/*
 * Return the current RFLAGS value.
 */
static inline uint64_t cpu_read_rflags(void)
{
    uint64_t flags;

    __asm__ volatile (
        "pushfq\n"
        "popq %0"
        : "=r"(flags)
        :
        : "memory"
    );

    return flags;
}


/*
 * Save the current interrupt state and disable maskable interrupts.
 */
static inline interrupt_state_t
interrupt_save_disable(void)
{
    interrupt_state_t state =
        cpu_read_rflags();

    __asm__ volatile (
        "cli"
        :
        :
        : "memory"
    );

    return state;
}


/*
 * Restore only the previous interrupt-enabled state.
 *
 * RFLAGS bit 9 is IF. We deliberately do not write all of RFLAGS,
 * because callers may have legitimately changed other flags.
 */
static inline void
interrupt_restore(interrupt_state_t state)
{
    if ((state & (UINT64_C(1) << 9)) != 0) {
        __asm__ volatile (
            "sti"
            :
            :
            : "memory"
        );
    }
}


static inline void
spinlock_init(spinlock_t *lock)
{
    __atomic_store_n(
        &lock->locked,
        0U,
        __ATOMIC_RELAXED
    );
}


static inline bool
spinlock_is_locked(const spinlock_t *lock)
{
    return __atomic_load_n(
        &lock->locked,
        __ATOMIC_RELAXED
    ) != 0U;
}


static inline void
spinlock_lock(spinlock_t *lock)
{
    for (;;) {
        if (__atomic_exchange_n(
                &lock->locked,
                1U,
                __ATOMIC_ACQUIRE
            ) == 0U) {
            return;
        }

        /*
         * Avoid repeatedly issuing locked writes while another CPU
         * owns the lock.
         */
        while (spinlock_is_locked(lock)) {
            __asm__ volatile ("pause");
        }
    }
}


static inline bool
spinlock_try_lock(spinlock_t *lock)
{
    uint32_t expected = 0U;

    return __atomic_compare_exchange_n(
        &lock->locked,
        &expected,
        1U,
        false,
        __ATOMIC_ACQUIRE,
        __ATOMIC_RELAXED
    );
}


static inline void
spinlock_unlock(spinlock_t *lock)
{
    __atomic_store_n(
        &lock->locked,
        0U,
        __ATOMIC_RELEASE
    );
}


static inline interrupt_state_t
spinlock_lock_irqsave(spinlock_t *lock)
{
    interrupt_state_t state =
        interrupt_save_disable();

    spinlock_lock(lock);

    return state;
}


static inline void
spinlock_unlock_irqrestore(
    spinlock_t *lock,
    interrupt_state_t state
)
{
    /*
     * Shared state must become visible before interrupts are restored.
     */
    spinlock_unlock(lock);
    interrupt_restore(state);
}

static inline bool
interrupts_are_enabled(void)
{
    return (
        cpu_read_rflags() &
        (UINT64_C(1) << 9)
    ) != 0;
}

#endif