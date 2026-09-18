#include "task/thread.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/context_switch.h"
#include "memory/heap.h"
#include "memory/kernel_stack.h"
#include "sync/spinlock.h"
#include "interrupts.h"
#include "arch/x86_64/gdt.h"
#include "memory/layout.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "task/process.h"

#define KERNEL_THREAD_DEFAULT_STACK_PAGES \
    ((size_t)4)

#define KERNEL_THREAD_TIME_SLICE_TICKS \
    UINT32_C(5)

struct kernel_thread {
    uint64_t id;

    enum kernel_thread_state state;

    /*
     * Process whose address space must be active while this thread
     * executes.
     */
    struct kernel_process *process;

    struct kernel_stack stack;

    /*
     * Value installed into TSS.RSP0 when this thread is selected.
     *
     * For future user threads, interrupts entering ring 0 will begin
     * at this stack top.
     */
    uint64_t ring0_stack_top;

    uint64_t *saved_rsp;

    kernel_thread_entry_t entry;
    void *argument;

    /*
    * Circular scheduler-queue link while the thread is READY,
    * RUNNING, or BLOCKED.
    *
    * After termination, this becomes the zombie-list link.
    */
    struct kernel_thread *next;
};


static struct kernel_thread boot_thread;

static struct kernel_thread *run_queue_head;
static struct kernel_thread *run_queue_tail;
static struct kernel_thread *current_thread;

static struct kernel_thread *zombie_list;

static uint64_t next_thread_id;
static uint64_t live_thread_count;

static bool thread_system_initialized;
static bool scheduler_faulted;

static struct kernel_thread *blocking_test_waiter;

static volatile uint8_t blocking_test_trace[3];
static volatile uint64_t blocking_test_trace_index;
static volatile bool blocking_test_enabled;
static volatile bool blocking_test_failed;

static spinlock_t scheduler_lock =
    SPINLOCK_INITIALIZER;

static uint32_t scheduler_slice_ticks;
static uint64_t scheduler_tick_count;

static volatile uint8_t timer_test_trace[6];
static volatile size_t timer_test_trace_index;
static volatile bool timer_test_enabled;
static volatile bool timer_test_failed;

static bool scheduler_preemption_enabled;

/*
 * Entered by RET from arch_context_switch() for a new thread.
 */
static _Noreturn void kernel_thread_bootstrap(void);

_Static_assert(
    sizeof(struct arch_interrupt_context) ==
        20 * sizeof(uint64_t),
    "Unexpected interrupt-context size"
);

_Static_assert(
    offsetof(
        struct arch_interrupt_context,
        instruction_pointer
    ) == 15 * sizeof(uint64_t),
    "Incorrect instruction-pointer offset"
);

_Static_assert(
    offsetof(
        struct arch_interrupt_context,
        stack_pointer
    ) == 18 * sizeof(uint64_t),
    "Incorrect stack-pointer offset"
);

_Static_assert(
    offsetof(
        struct arch_interrupt_context,
        stack_segment
    ) == 19 * sizeof(uint64_t),
    "Incorrect stack-segment offset"
);

/*
 * Free terminated threads only after execution has moved away from
 * their stacks.
 */
static void reap_terminated_threads(void)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &scheduler_lock
        );

    struct kernel_thread *list =
        zombie_list;

    zombie_list = NULL;

    spinlock_unlock_irqrestore(
        &scheduler_lock,
        interrupt_state
    );

    while (list != NULL) {
        struct kernel_thread *next =
            list->next;

        if (!kernel_stack_release(
                &list->stack
            )) {
            scheduler_faulted = true;
            list = next;
            continue;
        }

        /*
         * Release the process reference only after the thread has
         * completely stopped using its kernel stack and address
         * space.
         */
        if (!kernel_process_detach_thread(
                list->process
            )) {
            scheduler_faulted = true;
            list = next;
            continue;
        }

        list->process = NULL;

        if (!kfree(list)) {
            scheduler_faulted = true;
        }

        list = next;
    }
}


/*
 * Construct the register frame expected by context_switch.asm.
 *
 * The assembly restores:
 *
 *     R15, R14, R13, R12, RBX, RBP
 *
 * and then executes RET.
 */
static bool initialize_thread_context(
    struct kernel_thread *thread
)
{
    if (thread == NULL ||
        thread->stack.stack_top == 0) {
        return false;
    }

    uint16_t code_segment;
    uint16_t stack_segment;

    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(code_segment)
    );

    __asm__ volatile (
        "mov %%ss, %0"
        : "=r"(stack_segment)
    );

    /*
     * Preserve one fake return-address slot. IRETQ explicitly
     * restores RSP to this address, giving the bootstrap function
     * the System V entry alignment RSP % 16 == 8.
     */
    uint64_t *return_slot =
        (uint64_t *)(uintptr_t)(
            thread->stack.stack_top -
            sizeof(uint64_t)
        );

    *return_slot = 0;

    struct arch_interrupt_context *context =
        (struct arch_interrupt_context *)(void *)(
            (uint8_t *)return_slot -
            sizeof(struct arch_interrupt_context)
        );

    uint64_t initial_flags =
        UINT64_C(0x2);

    /*
     * Threads created after the APIC scheduler is enabled must allow
     * timer interrupts. RFLAGS bit 9 is IF.
     */
    if (__atomic_load_n(
            &scheduler_preemption_enabled,
            __ATOMIC_ACQUIRE
        )) {
        initial_flags |=
            UINT64_C(1) << 9;
    }

    *context =
        (struct arch_interrupt_context){
            .r15 = 0,
            .r14 = 0,
            .r13 = 0,
            .r12 = 0,
            .r11 = 0,
            .r10 = 0,
            .r9 = 0,
            .r8 = 0,
            .rdi = 0,
            .rsi = 0,
            .rbp = 0,
            .rdx = 0,
            .rcx = 0,
            .rbx = 0,
            .rax = 0,

            .instruction_pointer =
                (uint64_t)(uintptr_t)
                    kernel_thread_bootstrap,

            .code_segment =
                (uint64_t)code_segment,

            .flags =
                initial_flags,

            .stack_pointer =
                (uint64_t)(uintptr_t)
                    return_slot,

            .stack_segment =
                (uint64_t)stack_segment
        };

    thread->saved_rsp =
        (uint64_t *)(void *)context;

    return true;
}

static bool initialize_user_thread_context(
    struct kernel_thread *thread,
    uint64_t user_entry,
    uint64_t user_stack_top
)
{
    if (thread == NULL ||
        thread->process == NULL ||
        thread->stack.stack_top == 0 ||
        user_entry < USER_SPACE_BASE ||
        user_entry > USER_SPACE_TOP ||
        user_stack_top <= USER_SPACE_BASE ||
        user_stack_top > USER_SPACE_TOP ||
        (user_stack_top & UINT64_C(0x0F)) != 0) {
        return false;
    }

    struct vmm_address_space *space =
        kernel_process_address_space(
            thread->process
        );

    uint64_t ignored_physical;

    /*
     * Verify that both the entry point and top of the user stack are
     * backed by mapped pages.
     */
    if (space == NULL ||
        !vmm_translate(
            space,
            user_entry,
            &ignored_physical
        ) ||
        !vmm_translate(
            space,
            user_stack_top - sizeof(uint64_t),
            &ignored_physical
        )) {
        return false;
    }

    struct arch_interrupt_context *context =
        (struct arch_interrupt_context *)(uintptr_t)(
            thread->stack.stack_top -
            sizeof(struct arch_interrupt_context)
        );

    uint64_t initial_flags =
        UINT64_C(0x2);

    if (__atomic_load_n(
            &scheduler_preemption_enabled,
            __ATOMIC_ACQUIRE
        )) {
        initial_flags |=
            UINT64_C(1) << 9;
    }

    *context =
        (struct arch_interrupt_context){
            .r15 = 0,
            .r14 = 0,
            .r13 = 0,
            .r12 = 0,
            .r11 = 0,
            .r10 = 0,
            .r9 = 0,
            .r8 = 0,
            .rdi = 0,
            .rsi = 0,
            .rbp = 0,
            .rdx = 0,
            .rcx = 0,
            .rbx = 0,
            .rax = 0,

            .instruction_pointer =
                user_entry,

            .code_segment =
                GDT_USER_CODE_SELECTOR,

            .flags =
                initial_flags,

            .stack_pointer =
                user_stack_top,

            .stack_segment =
                GDT_USER_DATA_SELECTOR
        };

    thread->saved_rsp =
        (uint64_t *)(void *)context;

    return true;
}

static bool thread_is_queued_locked(const struct kernel_thread *thread) {
    struct kernel_thread *candidate;

    if (thread == NULL || run_queue_head == NULL) {
        return false;
    }

    candidate = run_queue_head;

    do {
        if (candidate == thread) {
            return true;
        }

        candidate = candidate->next;
    } while (candidate != run_queue_head);

    return false;
}

bool kernel_thread_system_init(void)
{
    struct kernel_process *owner =
    kernel_process_kernel();

    if (owner == NULL) {
        return false;
    }
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &scheduler_lock
        );

    if (thread_system_initialized ||
    !interrupts_install_gate(
        ARCH_RESCHEDULE_VECTOR,
        (uintptr_t)
            arch_reschedule_interrupt_entry
    )) {
        spinlock_unlock_irqrestore(
            &scheduler_lock,
            interrupt_state
        );

        return false;
    }
    if (!kernel_process_attach_thread(owner)) {
    spinlock_unlock_irqrestore(
        &scheduler_lock,
        interrupt_state
    );

    return false;
    }

    boot_thread =
        (struct kernel_thread){
            .id = 0,

            .state =
                KERNEL_THREAD_RUNNING,

            .process = owner,

            /*
             * The boot thread has no dynamically allocated guarded
             * stack. Use the bootstrap privilege stack installed by
             * gdt_init() for its TSS.RSP0 value.
             */
            .ring0_stack_top =
                gdt_kernel_stack(),

            .saved_rsp = NULL,
            .entry = NULL,
            .argument = NULL,
            .next = &boot_thread
        };

    if (boot_thread.ring0_stack_top == 0) {
        (void)kernel_process_detach_thread(
            owner
        );

        spinlock_unlock_irqrestore(
            &scheduler_lock,
            interrupt_state
        );

        return false;
    }

    run_queue_head =
        &boot_thread;

    run_queue_tail =
        &boot_thread;

    current_thread =
        &boot_thread;

    zombie_list = NULL;

    next_thread_id = 1;
    live_thread_count = 0;
    scheduler_faulted = false;

    scheduler_slice_ticks = 0;
    scheduler_tick_count = 0;
    scheduler_preemption_enabled = false;
    thread_system_initialized = true;

    spinlock_unlock_irqrestore(
        &scheduler_lock,
        interrupt_state
    );

    return true;
}

bool kernel_thread_enable_preemption(void)
{
    if (!thread_system_initialized) {
        return false;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &scheduler_lock
        );

    if (scheduler_preemption_enabled) {
        spinlock_unlock_irqrestore(
            &scheduler_lock,
            interrupt_state
        );

        return false;
    }

    /*
     * Start the first preemptive time slice cleanly.
     */
    scheduler_slice_ticks = 0;

    __atomic_store_n(
        &scheduler_preemption_enabled,
        true,
        __ATOMIC_RELEASE
    );

    spinlock_unlock_irqrestore(
        &scheduler_lock,
        interrupt_state
    );

    return true;
}


/*
 * Called externally by the Local APIC assembly-backed interrupt
 * dispatcher. This function must not be static.
 */
uint64_t *kernel_thread_timer_interrupt(
    uint64_t *interrupted_rsp
)
{
    if (!thread_system_initialized ||
        interrupted_rsp == NULL) {
        return interrupted_rsp;
    }

    __atomic_add_fetch(
        &scheduler_tick_count,
        UINT64_C(1),
        __ATOMIC_RELAXED
    );

    /*
     * The APIC timer may be active before forced scheduling is
     * enabled. Count those ticks without switching threads.
     */
    if (!__atomic_load_n(
            &scheduler_preemption_enabled,
            __ATOMIC_ACQUIRE
        )) {
        return interrupted_rsp;
    }

    uint32_t slice_ticks =
        __atomic_add_fetch(
            &scheduler_slice_ticks,
            UINT32_C(1),
            __ATOMIC_RELAXED
        );

    if (slice_ticks <
        KERNEL_THREAD_TIME_SLICE_TICKS) {
        return interrupted_rsp;
    }

    __atomic_store_n(
        &scheduler_slice_ticks,
        UINT32_C(0),
        __ATOMIC_RELAXED
    );

    return kernel_thread_reschedule_interrupt(
        interrupted_rsp
    );
}

struct kernel_thread *kernel_thread_create_for_process(
    struct kernel_process *process,
    kernel_thread_entry_t entry,
    void *argument,
    size_t stack_pages
)
{
    if (!thread_system_initialized ||
        process == NULL ||
        entry == NULL ||
        stack_pages == 0 ||
        kernel_process_address_space(
            process
        ) == NULL) {
        return NULL;
    }

    /*
     * Take the process reference first. This prevents another thread
     * from destroying the process while allocation temporarily
     * allows preemption.
     */
    if (!kernel_process_attach_thread(
            process
        )) {
        return NULL;
    }

    struct kernel_thread *thread =
        kcalloc(
            1,
            sizeof(struct kernel_thread)
        );

    if (thread == NULL) {
        (void)kernel_process_detach_thread(
            process
        );

        return NULL;
    }

    if (!kernel_stack_allocate(
            stack_pages,
            &thread->stack
        )) {
        (void)kfree(thread);

        (void)kernel_process_detach_thread(
            process
        );

        return NULL;
    }

    thread->process = process;

    thread->ring0_stack_top =
        thread->stack.stack_top;

    thread->entry = entry;
    thread->argument = argument;

    thread->state =
        KERNEL_THREAD_READY;

    if (!initialize_thread_context(
            thread
        )) {
        (void)kernel_stack_release(
            &thread->stack
        );

        (void)kfree(thread);

        (void)kernel_process_detach_thread(
            process
        );

        return NULL;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &scheduler_lock
        );

    thread->id =
        next_thread_id++;

    thread->next =
        run_queue_head;

    run_queue_tail->next =
        thread;

    run_queue_tail =
        thread;

    live_thread_count++;

    spinlock_unlock_irqrestore(
        &scheduler_lock,
        interrupt_state
    );

    return thread;
}

struct kernel_thread *kernel_thread_create_user(
    struct kernel_process *process,
    uint64_t user_entry,
    uint64_t user_stack_top,
    size_t kernel_stack_pages
)
{
    if (!thread_system_initialized ||
        process == NULL ||
        kernel_stack_pages == 0 ||
        kernel_process_address_space(process) == NULL) {
        return NULL;
    }

    if (!kernel_process_attach_thread(process)) {
        return NULL;
    }

    struct kernel_thread *thread =
        kcalloc(
            1,
            sizeof(struct kernel_thread)
        );

    if (thread == NULL) {
        (void)kernel_process_detach_thread(process);
        return NULL;
    }

    if (!kernel_stack_allocate(
            kernel_stack_pages,
            &thread->stack
        )) {
        (void)kfree(thread);
        (void)kernel_process_detach_thread(process);
        return NULL;
    }

    thread->process = process;

    thread->ring0_stack_top =
        thread->stack.stack_top;

    thread->entry = NULL;
    thread->argument = NULL;
    thread->state = KERNEL_THREAD_READY;

    if (!initialize_user_thread_context(
            thread,
            user_entry,
            user_stack_top
        )) {
        (void)kernel_stack_release(
            &thread->stack
        );

        (void)kfree(thread);

        (void)kernel_process_detach_thread(
            process
        );

        return NULL;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &scheduler_lock
        );

    thread->id =
        next_thread_id++;

    thread->next =
        run_queue_head;

    run_queue_tail->next =
        thread;

    run_queue_tail =
        thread;

    live_thread_count++;

    spinlock_unlock_irqrestore(
        &scheduler_lock,
        interrupt_state
    );

    return thread;
}

struct kernel_thread *kernel_thread_create(
    kernel_thread_entry_t entry,
    void *argument,
    size_t stack_pages
)
{
    return kernel_thread_create_for_process(
        kernel_process_kernel(),
        entry,
        argument,
        stack_pages
    );
}



/*
 * Select another runnable context.
 *
 * The caller holds scheduler_lock and interrupts are disabled by the
 * interrupt gate.
 */
static uint64_t *schedule_locked(
    uint64_t *interrupted_rsp
)
{
    struct kernel_thread *previous =
        current_thread;

    if (previous == NULL ||
        interrupted_rsp == NULL) {
        scheduler_faulted = true;
        return interrupted_rsp;
    }

    previous->saved_rsp =
        interrupted_rsp;

    /*
     * A terminated thread has already been removed from the run
     * queue. Otherwise continue searching after the current thread.
     */
    struct kernel_thread *start =
        previous->state ==
            KERNEL_THREAD_TERMINATED
            ? run_queue_head
            : previous->next;

    struct kernel_thread *candidate =
        start;

    if (candidate != NULL) {
        do {
            if (candidate->state ==
                KERNEL_THREAD_READY) {
                break;
            }

            candidate =
                candidate->next;
        } while (candidate != start);
    }

    /*
     * No alternative runnable thread exists.
     */
    if (candidate == NULL ||
        candidate->state !=
            KERNEL_THREAD_READY) {
        if (previous->state !=
            KERNEL_THREAD_RUNNING) {
            scheduler_faulted = true;
        }

        return interrupted_rsp;
    }

        struct vmm_address_space *candidate_space =
        kernel_process_address_space(
            candidate->process
        );

    if (candidate_space == NULL ||
        candidate->saved_rsp == NULL ||
        candidate->ring0_stack_top == 0) {
        scheduler_faulted = true;
        return interrupted_rsp;
    }

    /*
     * The TSS tells the CPU where to begin the ring-0 stack when an
     * interrupt arrives from this thread in ring 3.
     */
    if (!gdt_set_kernel_stack(
            candidate->ring0_stack_top
        )) {
        scheduler_faulted = true;
        return interrupted_rsp;
    }

    /*
     * Loading CR3 selects the candidate process's translations.
     *
     * Both the outgoing and incoming kernel stacks are in the shared
     * higher-half kernel mapping, so they remain valid across this
     * operation.
     */
    if (!vmm_address_space_is_active(
            candidate_space
        )) {
        vmm_activate(candidate_space);
    }

    if (!vmm_address_space_is_active(
            candidate_space
        )) {
        scheduler_faulted = true;
        return interrupted_rsp;
    }

    if (previous->state ==
        KERNEL_THREAD_RUNNING) {
        previous->state =
            KERNEL_THREAD_READY;
    }

    candidate->state =
        KERNEL_THREAD_RUNNING;

    current_thread =
        candidate;

    return candidate->saved_rsp;
}


uint64_t *kernel_thread_reschedule_interrupt(
    uint64_t *interrupted_rsp
)
{
    if (!thread_system_initialized ||
        interrupted_rsp == NULL) {
        return interrupted_rsp;
    }

    /*
     * Every ordinary scheduler-lock acquisition disables interrupts
     * first. Therefore the interrupt cannot have interrupted a
     * scheduler-lock owner on this CPU.
     */
    spinlock_lock(
        &scheduler_lock
    );

    uint64_t *resume_rsp =
        schedule_locked(
            interrupted_rsp
        );

    spinlock_unlock(
        &scheduler_lock
    );

    return resume_rsp;
}


void kernel_thread_yield(void)
{
    if (!thread_system_initialized) {
        return;
    }

    arch_request_context_switch();

    /*
     * This executes after the yielding thread is selected again.
     */
    reap_terminated_threads();
}

bool kernel_thread_block(void) {
    struct kernel_thread *previous;
    struct kernel_thread *next;
    uint64_t flags;

    if (!thread_system_initialized) {
        return false;
    }

    flags = spinlock_lock_irqsave(&scheduler_lock);

    previous = current_thread;

    /*
     * The boot thread is currently the scheduler's fallback thread.
     * It must remain runnable until a dedicated idle thread exists.
     */
    if (previous == NULL ||
        previous == &boot_thread ||
        previous->state != KERNEL_THREAD_RUNNING) {
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return false;
    }

    next = previous->next;

    while (next != previous &&
           next->state != KERNEL_THREAD_READY) {
        next = next->next;
    }

    if (next == previous ||
        next->state != KERNEL_THREAD_READY) {
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return false;
    }

    previous->state =
        KERNEL_THREAD_BLOCKED;

    /*
    * Release the lock but keep interrupts disabled until the blocked
    * thread has entered the scheduler.
    */
    spinlock_unlock(
        &scheduler_lock
    );

    arch_request_context_switch();

    /*
    * Execution returns here only after another thread wakes this one.
    */
    interrupt_restore(flags);

    reap_terminated_threads();

    return true;
}

bool kernel_thread_wake(struct kernel_thread *thread) {
    uint64_t flags;
    bool woke_thread = false;

    if (!thread_system_initialized || thread == NULL) {
        return false;
    }

    flags = spinlock_lock_irqsave(&scheduler_lock);

    if (thread_is_queued_locked(thread) &&
        thread->state == KERNEL_THREAD_BLOCKED) {
        thread->state = KERNEL_THREAD_READY;
        woke_thread = true;
    }

    spinlock_unlock_irqrestore(&scheduler_lock, flags);

    return woke_thread;
}

_Noreturn void kernel_thread_exit(void)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &scheduler_lock
        );

    struct kernel_thread *previous =
        current_thread;

    /*
     * Thread zero owns the original boot stack and cannot be removed.
     */
    if (previous == &boot_thread) {
        spinlock_unlock_irqrestore(
            &scheduler_lock,
            interrupt_state
        );

        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    /*
     * Find the previous run-queue node.
     */
    struct kernel_thread *predecessor =
        run_queue_head;

    while (predecessor->next !=
           previous) {
        predecessor =
            predecessor->next;
    }

    struct kernel_thread *next =
        previous->next;

    predecessor->next =
        next;

    if (run_queue_head == previous) {
        run_queue_head = next;
    }

    if (run_queue_tail == previous) {
        run_queue_tail =
            predecessor;
    }

    run_queue_tail->next =
        run_queue_head;

    previous->state =
        KERNEL_THREAD_TERMINATED;

    /*
     * It cannot be freed yet because the CPU is still executing on
     * previous->stack.
     */
    previous->next =
        zombie_list;

    zombie_list =
        previous;

    live_thread_count--;

    /*
    * Leave interrupts disabled. The terminated thread must not execute
    * again after entering the scheduler.
    */
    spinlock_unlock(
        &scheduler_lock
    );

    arch_request_context_switch();

    /*
    * A terminated thread must never return here.
    */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}


uint64_t kernel_thread_current_id(void)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &scheduler_lock
        );

    uint64_t id =
        current_thread != NULL
            ? current_thread->id
            : UINT64_MAX;

    spinlock_unlock_irqrestore(
        &scheduler_lock,
        interrupt_state
    );

    return id;
}


uint64_t kernel_thread_live_count(void)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &scheduler_lock
        );

    uint64_t count =
        live_thread_count;

    spinlock_unlock_irqrestore(
        &scheduler_lock,
        interrupt_state
    );

    return count;
}


void kernel_thread_preemption_point(void)
{
    if (!thread_system_initialized) {
        return;
    }

    /*
     * Context switching is now automatic. This function remains as
     * a safe place to free terminated-thread stacks.
     */
    reap_terminated_threads();
}


uint64_t kernel_thread_scheduler_ticks(void)
{
    return __atomic_load_n(
        &scheduler_tick_count,
        __ATOMIC_RELAXED
    );
}

static _Noreturn void kernel_thread_bootstrap(void)
{
    /*
     * The outgoing thread is no longer using any zombie stack.
     */
    reap_terminated_threads();

    struct kernel_thread *thread =
        current_thread;

    if (thread == NULL ||
        thread->entry == NULL) {
        scheduler_faulted = true;
        kernel_thread_exit();
    }

    thread->entry(
        thread->argument
    );

    kernel_thread_exit();
}

uint64_t kernel_thread_current_process_id(void)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &scheduler_lock
        );

    uint64_t process_id =
        current_thread != NULL &&
        current_thread->process != NULL
            ? kernel_process_id(
                current_thread->process
            )
            : UINT64_MAX;

    spinlock_unlock_irqrestore(
        &scheduler_lock,
        interrupt_state
    );

    return process_id;
}

/*
 * Round-robin execution trace:
 *
 *     thread A writes 1
 *     thread B writes 2
 */
static volatile uint8_t test_trace[8];
static volatile size_t test_trace_index;


static void test_thread_entry(
    void *argument
)
{
    uint8_t marker =
        (uint8_t)(uintptr_t)argument;

    for (size_t iteration = 0;
         iteration < 4;
         iteration++) {
        size_t index =
            test_trace_index;

        if (index <
            sizeof(test_trace)) {
            test_trace[index] =
                marker;

            test_trace_index =
                index + 1;
        } else {
            scheduler_faulted = true;
        }

        kernel_thread_yield();
    }
}


bool kernel_thread_self_test(void)
{
    if (!thread_system_initialized) {
        return false;
    }

    for (size_t index = 0;
         index < sizeof(test_trace);
         index++) {
        test_trace[index] = 0;
    }

    test_trace_index = 0;
    scheduler_faulted = false;

    struct kernel_thread *thread_a =
        kernel_thread_create(
            test_thread_entry,
            (void *)(uintptr_t)1,
            4
        );

    struct kernel_thread *thread_b =
        kernel_thread_create(
            test_thread_entry,
            (void *)(uintptr_t)2,
            4
        );

    bool creation_passed =
        thread_a != NULL &&
        thread_b != NULL;

    /*
     * Keep yielding the boot thread until every dynamic test thread
     * terminates.
     */
    while (kernel_thread_live_count() >
           0) {
        kernel_thread_yield();
    }

    /*
     * The final return to the boot thread reaps the last zombie.
     */
    reap_terminated_threads();

    bool sequence_passed =
        test_trace_index ==
            sizeof(test_trace);

    if (sequence_passed) {
        for (size_t index = 0;
             index < sizeof(test_trace);
             index++) {
            uint8_t expected =
                (index & 1) == 0
                    ? 1
                    : 2;

            if (test_trace[index] !=
                expected) {
                sequence_passed = false;
                break;
            }
        }
    }

    return creation_passed &&
        sequence_passed &&
        !scheduler_faulted &&
        kernel_thread_live_count() == 0;
}
static void blocking_test_waiter_entry(void *argument) {
    (void)argument;

    if (!blocking_test_enabled) {
        return;
    }

    blocking_test_trace[blocking_test_trace_index++] = 1;

    if (!kernel_thread_block()) {
        blocking_test_failed = true;
        return;
    }

    blocking_test_trace[blocking_test_trace_index++] = 3;
}

static void blocking_test_waker_entry(void *argument) {
    (void)argument;

    if (!blocking_test_enabled) {
        return;
    }

    blocking_test_trace[blocking_test_trace_index++] = 2;

    if (!kernel_thread_wake(blocking_test_waiter)) {
        blocking_test_failed = true;
        return;
    }

    /*
     * Give the boot thread and then the newly awakened waiter an
     * opportunity to run.
     */
    kernel_thread_yield();
}
bool kernel_thread_blocking_self_test(void) {
    struct kernel_thread *waker;
    uint64_t yield_count = 0;
    bool creation_succeeded;
    bool trace_succeeded;

    if (!thread_system_initialized ||
        current_thread != &boot_thread) {
        return false;
    }

    blocking_test_waiter = NULL;
    blocking_test_trace_index = 0;
    blocking_test_enabled = false;
    blocking_test_failed = false;

    blocking_test_trace[0] = 0;
    blocking_test_trace[1] = 0;
    blocking_test_trace[2] = 0;

    blocking_test_waiter =
        kernel_thread_create(blocking_test_waiter_entry, NULL, KERNEL_THREAD_DEFAULT_STACK_PAGES);

    waker =
        kernel_thread_create(blocking_test_waker_entry, NULL, KERNEL_THREAD_DEFAULT_STACK_PAGES);

    creation_succeeded =
        blocking_test_waiter != NULL &&
        waker != NULL;

    /*
     * If either allocation failed, any successfully created test
     * thread will simply return instead of blocking forever.
     */
    blocking_test_enabled = creation_succeeded;

    /*
     * The limit turns scheduler errors into a failed test rather
     * than an infinite boot hang.
     */
    while (kernel_thread_live_count() != 0 &&
           yield_count < 64) {
        kernel_thread_yield();
        yield_count++;
    }

    reap_terminated_threads();

    trace_succeeded =
        blocking_test_trace_index == 3 &&
        blocking_test_trace[0] == 1 &&
        blocking_test_trace[1] == 2 &&
        blocking_test_trace[2] == 3;

    blocking_test_enabled = false;
    blocking_test_waiter = NULL;

    return creation_succeeded &&
           !blocking_test_failed &&
           kernel_thread_live_count() == 0 &&
           trace_succeeded;
}
static void timer_test_thread_entry(
    void *argument
)
{
    uint8_t marker =
        (uint8_t)(uintptr_t)argument;

    if (!timer_test_enabled) {
        return;
    }

    for (size_t iteration = 0;
         iteration < 3;
         iteration++) {
        size_t index =
            timer_test_trace_index;

        if (index >=
            sizeof(timer_test_trace)) {
            timer_test_failed = true;
            return;
        }

        timer_test_trace[index] =
            marker;

        timer_test_trace_index =
            index + 1;

        /*
            * Remain active for one complete scheduler time slice.
            *
            * There is deliberately no yield or preemption-point call here.
            * The APIC interrupt must forcibly switch this thread.
        */
        uint64_t slice_start =
            kernel_thread_scheduler_ticks();

        while ((kernel_thread_scheduler_ticks() -
                slice_start) <
               KERNEL_THREAD_TIME_SLICE_TICKS) {
            __asm__ volatile ("pause");
        }

    }
}
bool kernel_thread_timer_self_test(void)
{
    if (!thread_system_initialized ||
        current_thread != &boot_thread) {
        return false;
    }

    for (size_t index = 0;
         index < sizeof(timer_test_trace);
         index++) {
        timer_test_trace[index] = 0;
    }

    timer_test_trace_index = 0;
    timer_test_enabled = false;
    timer_test_failed = false;

    /*
        * Do not let the first thread run before the second thread and the
        * shared test state are fully initialized.
    */
    interrupt_state_t creation_interrupt_state =
        interrupt_save_disable();

    struct kernel_thread *thread_a =
        kernel_thread_create(
            timer_test_thread_entry,
            (void *)(uintptr_t)1,
            KERNEL_THREAD_DEFAULT_STACK_PAGES
        );

    struct kernel_thread *thread_b =
        kernel_thread_create(
            timer_test_thread_entry,
            (void *)(uintptr_t)2,
            KERNEL_THREAD_DEFAULT_STACK_PAGES
        );

    bool creation_succeeded =
        thread_a != NULL &&
        thread_b != NULL;

    /*
     * If only one allocation succeeded, allow that thread to run
     * and terminate without entering the actual test.
     */
    timer_test_enabled =
        creation_succeeded;

    interrupt_restore(
        creation_interrupt_state
    );

    uint64_t timeout_start =
        kernel_thread_scheduler_ticks();

    while (kernel_thread_live_count() != 0 &&
           (kernel_thread_scheduler_ticks() -
            timeout_start) < UINT64_C(300)) {
        /*
         * The boot thread also changes context only when its timer
         * time slice expires.
         */
        kernel_thread_preemption_point();

        __asm__ volatile ("pause");
    }

    reap_terminated_threads();

    bool trace_succeeded =
        timer_test_trace_index ==
            sizeof(timer_test_trace);

    if (trace_succeeded) {
        for (size_t index = 0;
             index < sizeof(timer_test_trace);
             index++) {
            uint8_t expected =
                (index & 1U) == 0
                    ? UINT8_C(1)
                    : UINT8_C(2);

            if (timer_test_trace[index] !=
                expected) {
                trace_succeeded = false;
                break;
            }
        }
    }

    bool all_threads_terminated =
        kernel_thread_live_count() == 0;

    timer_test_enabled = false;

    return creation_succeeded &&
        trace_succeeded &&
        all_threads_terminated &&
        !timer_test_failed &&
        !scheduler_faulted;
}
struct process_thread_test_context {
    uint64_t process_id;
    uint64_t value;

    volatile bool completed;
};

static volatile bool process_thread_test_failed;

static void process_thread_test_entry(
    void *argument
)
{
    struct process_thread_test_context *context =
        (struct process_thread_test_context *)
            argument;

    if (context == NULL ||
        kernel_thread_current_process_id() !=
            context->process_id) {
        process_thread_test_failed = true;
        return;
    }

    volatile uint64_t *private_value =
        (volatile uint64_t *)(uintptr_t)
            USER_SPACE_BASE;

    for (size_t iteration = 0;
         iteration < 4;
         iteration++) {
        *private_value =
            context->value;

        kernel_thread_yield();

        /*
         * The other process used the exact same virtual address.
         * Its write must have reached a different physical frame.
         */
        if (*private_value !=
            context->value) {
            process_thread_test_failed = true;
            return;
        }
    }

    context->completed = true;
}
bool kernel_thread_process_self_test(void)
{
    if (!thread_system_initialized) {
        return false;
    }

    struct pmm_statistics before;
    struct pmm_statistics after;

    pmm_get_statistics(&before);

    struct kernel_process *process_a =
        NULL;

    struct kernel_process *process_b =
        NULL;

    uint64_t frame_a =
        PMM_INVALID_ADDRESS;

    uint64_t frame_b =
        PMM_INVALID_ADDRESS;

    bool mapped_a = false;
    bool mapped_b = false;
    bool passed = false;

    uint64_t live_before =
        kernel_thread_live_count();

    process_thread_test_failed = false;

    process_a =
        kernel_process_create();

    process_b =
        kernel_process_create();

    if (process_a == NULL ||
        process_b == NULL) {
        goto cleanup;
    }

    frame_a = pmm_allocate_page();
    frame_b = pmm_allocate_page();

    if (frame_a == PMM_INVALID_ADDRESS ||
        frame_b == PMM_INVALID_ADDRESS ||
        frame_a == frame_b) {
        goto cleanup;
    }

    void *frame_a_virtual =
        pmm_physical_to_virtual(frame_a);

    void *frame_b_virtual =
        pmm_physical_to_virtual(frame_b);

    if (frame_a_virtual == NULL ||
        frame_b_virtual == NULL) {
        goto cleanup;
    }

    /*
     * Never expose uninitialized physical memory to a process.
     */
    for (size_t index = 0;
         index < VMM_PAGE_SIZE;
         index++) {
        ((uint8_t *)frame_a_virtual)[index] = 0;
        ((uint8_t *)frame_b_virtual)[index] = 0;
    }

    uint64_t flags =
        VMM_PAGE_USER |
        VMM_PAGE_WRITABLE;

    if (vmm_nx_supported()) {
        flags |= VMM_PAGE_NO_EXECUTE;
    }

    struct vmm_address_space *space_a =
        kernel_process_address_space(
            process_a
        );

    struct vmm_address_space *space_b =
        kernel_process_address_space(
            process_b
        );

    if (space_a == NULL ||
        space_b == NULL) {
        goto cleanup;
    }

    if (!vmm_map_page(
            space_a,
            USER_SPACE_BASE,
            frame_a,
            flags
        )) {
        goto cleanup;
    }

    mapped_a = true;

    if (!vmm_map_page(
            space_b,
            USER_SPACE_BASE,
            frame_b,
            flags
        )) {
        goto cleanup;
    }

    mapped_b = true;

    struct process_thread_test_context context_a = {
        .process_id =
            kernel_process_id(process_a),

        .value =
            UINT64_C(0xAAAAAAAAAAAAAAAA),

        .completed = false
    };

    struct process_thread_test_context context_b = {
        .process_id =
            kernel_process_id(process_b),

        .value =
            UINT64_C(0xBBBBBBBBBBBBBBBB),

        .completed = false
    };

    struct kernel_thread *thread_a =
        kernel_thread_create_for_process(
            process_a,
            process_thread_test_entry,
            &context_a,
            4
        );

    struct kernel_thread *thread_b =
        kernel_thread_create_for_process(
            process_b,
            process_thread_test_entry,
            &context_b,
            4
        );

    if (thread_a == NULL ||
        thread_b == NULL) {
        /*
         * A successfully created thread still needs to execute and
         * terminate before its process can be cleaned up.
         */
        while (kernel_thread_live_count() >
               live_before) {
            kernel_thread_yield();
        }

        goto cleanup;
    }

    while (kernel_thread_live_count() >
           live_before) {
        kernel_thread_yield();
    }

    kernel_thread_preemption_point();

    /*
     * We must have returned to PID 0 and its page tables.
     */
    if (kernel_thread_current_process_id() != 0 ||
        !vmm_address_space_is_active(
            vmm_kernel_address_space()
        )) {
        goto cleanup;
    }

    if (!context_a.completed ||
        !context_b.completed ||
        process_thread_test_failed ||
        kernel_process_thread_count(
            process_a
        ) != 0 ||
        kernel_process_thread_count(
            process_b
        ) != 0) {
        goto cleanup;
    }

    /*
     * Each process wrote to the same virtual address, but different
     * physical frames must contain different values.
     */
    if (*(uint64_t *)frame_a_virtual !=
            context_a.value ||
        *(uint64_t *)frame_b_virtual !=
            context_b.value) {
        goto cleanup;
    }

    passed = true;

cleanup:
    /*
     * Complete any partially created threads before releasing their
     * process address spaces.
     */
    while (kernel_thread_live_count() >
           live_before) {
        kernel_thread_yield();
    }

    kernel_thread_preemption_point();

    if (mapped_a && process_a != NULL) {
        uint64_t removed;

        if (!vmm_unmap_page(
                kernel_process_address_space(
                    process_a
                ),
                USER_SPACE_BASE,
                &removed
            ) ||
            removed != frame_a) {
            passed = false;
        } else {
            mapped_a = false;
        }
    }

    if (mapped_b && process_b != NULL) {
        uint64_t removed;

        if (!vmm_unmap_page(
                kernel_process_address_space(
                    process_b
                ),
                USER_SPACE_BASE,
                &removed
            ) ||
            removed != frame_b) {
            passed = false;
        } else {
            mapped_b = false;
        }
    }

    if (!mapped_a &&
        frame_a != PMM_INVALID_ADDRESS) {
        if (!pmm_free_page(frame_a)) {
            passed = false;
        }

        frame_a = PMM_INVALID_ADDRESS;
    }

    if (!mapped_b &&
        frame_b != PMM_INVALID_ADDRESS) {
        if (!pmm_free_page(frame_b)) {
            passed = false;
        }

        frame_b = PMM_INVALID_ADDRESS;
    }

    if (process_a != NULL &&
        !kernel_process_destroy(process_a)) {
        passed = false;
    }

    if (process_b != NULL &&
        !kernel_process_destroy(process_b)) {
        passed = false;
    }

    pmm_get_statistics(&after);

    return passed &&
        !process_thread_test_failed &&
        before.free_pages ==
            after.free_pages;
}