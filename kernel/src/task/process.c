#include "task/process.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/heap.h"
#include "memory/layout.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "sync/spinlock.h"
#include "object/object.h"

struct kernel_process_handle {
    struct kernel_object *object;
    uint32_t rights;
};

struct kernel_process {
    uint64_t id;

    enum kernel_process_state state;

    struct vmm_address_space address_space;

    size_t thread_count;

    struct kernel_process_handle handles[KERNEL_PROCESS_MAX_HANDLES];

    /*
     * Process-list linkage.
     */
    struct kernel_process *next;
};

static struct kernel_process kernel_process;

static struct kernel_process *process_list;
static uint64_t next_process_id;
static bool process_system_initialized;

static spinlock_t process_lock =
    SPINLOCK_INITIALIZER;

static bool process_registered_locked(
    const struct kernel_process *process
)
{
    const struct kernel_process *candidate =
        process_list;

    while (candidate != NULL) {
        if (candidate == process) {
            return true;
        }

        candidate = candidate->next;
    }

    return false;
}

bool kernel_process_system_init(void)
{
    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &process_lock
        );

    struct vmm_address_space *kernel_space =
        vmm_kernel_address_space();

    if (process_system_initialized ||
        kernel_space == NULL ||
        kernel_space->pml4_physical == 0) {
        spinlock_unlock_irqrestore(
            &process_lock,
            interrupt_state
        );

        return false;
    }

    kernel_process =
        (struct kernel_process){
            .id = 0,
            .state =
                KERNEL_PROCESS_ALIVE,

            /*
             * PID 0 refers to the existing kernel address space. It
             * does not own or destroy this page-table hierarchy.
             */
            .address_space =
                *kernel_space,

            .thread_count = 0,
            .next = NULL
        };

    process_list =
        &kernel_process;

    next_process_id = 1;
    process_system_initialized = true;

    spinlock_unlock_irqrestore(
        &process_lock,
        interrupt_state
    );

    return true;
}

struct kernel_process *kernel_process_create(void)
{
    if (!process_system_initialized) {
        return NULL;
    }

    struct kernel_process *process =
        kcalloc(
            1,
            sizeof(struct kernel_process)
        );

    if (process == NULL) {
        return NULL;
    }

    if (!vmm_address_space_create(
            &process->address_space
        )) {
        kfree(process);
        return NULL;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &process_lock
        );

    process->id =
        next_process_id++;

    process->state =
        KERNEL_PROCESS_ALIVE;

    process->thread_count = 0;

    process->next =
        process_list;

    process_list =
        process;

    spinlock_unlock_irqrestore(
        &process_lock,
        interrupt_state
    );

    return process;
}

bool kernel_process_handle_install(
    struct kernel_process *process,
    uint64_t handle,
    struct kernel_object *object,
    uint32_t rights
)
{
    if (!process_system_initialized ||
        process == NULL ||
        object == NULL ||
        handle >= KERNEL_PROCESS_MAX_HANDLES ||
        rights == 0) {
        return false;
    }

    /*
     * Obtain the handle table's reference before publishing the
     * object in the process.
     */
    if (!kernel_object_retain(object)) {
        return false;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &process_lock
        );

    bool installed = false;

    if (process_registered_locked(process) &&
        process->state ==
            KERNEL_PROCESS_ALIVE &&
        process->handles[handle].object ==
            NULL) {
        process->handles[handle].object =
            object;

        process->handles[handle].rights =
            rights;

        installed = true;
    }

    spinlock_unlock_irqrestore(
        &process_lock,
        interrupt_state
    );

    if (!installed) {
        kernel_object_release(object);
    }

    return installed;
}

bool kernel_process_handle_allocate(
    struct kernel_process *process,
    struct kernel_object *object,
    uint32_t rights,
    uint64_t *result_handle
)
{
    if (!process_system_initialized ||
        process == NULL ||
        object == NULL ||
        result_handle == NULL ||
        rights == 0) {
        return false;
    }

    if (!kernel_object_retain(object)) {
        return false;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(&process_lock);

    bool installed = false;
    uint64_t selected_handle = UINT64_MAX;

    if (process_registered_locked(process) &&
        process->state == KERNEL_PROCESS_ALIVE) {
        for (uint64_t handle =
                 KERNEL_PROCESS_FIRST_DYNAMIC_HANDLE;
             handle < KERNEL_PROCESS_MAX_HANDLES;
             handle++) {
            if (process->handles[handle].object == NULL) {
                process->handles[handle].object = object;
                process->handles[handle].rights = rights;

                selected_handle = handle;
                installed = true;
                break;
            }
        }
    }

    spinlock_unlock_irqrestore(
        &process_lock,
        interrupt_state
    );

    if (!installed) {
        kernel_object_release(object);
        return false;
    }

    *result_handle = selected_handle;
    return true;
}

bool kernel_process_handle_duplicate(
    struct kernel_process *source_process,
    uint64_t source_handle,
    struct kernel_process *target_process,
    uint64_t target_handle,
    uint32_t rights
)
{
    if (source_process == NULL ||
        target_process == NULL ||
        rights == 0) {
        return false;
    }

    /*
     * acquire() also verifies that the source handle owns every
     * requested right, preventing rights escalation.
     */
    struct kernel_object *object =
        kernel_process_handle_acquire(
            source_process,
            source_handle,
            rights
        );

    if (object == NULL) {
        return false;
    }

    bool installed =
        kernel_process_handle_install(
            target_process,
            target_handle,
            object,
            rights
        );

    kernel_object_release(object);
    return installed;
}

struct kernel_object *kernel_process_handle_acquire(
    struct kernel_process *process,
    uint64_t handle,
    uint32_t required_rights
)
{
    if (!process_system_initialized ||
        process == NULL ||
        handle >= KERNEL_PROCESS_MAX_HANDLES) {
        return NULL;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &process_lock
        );

    struct kernel_object *object = NULL;

    struct kernel_process_handle *entry =
        &process->handles[handle];

    if (process_registered_locked(process) &&
        process->state ==
            KERNEL_PROCESS_ALIVE &&
        entry->object != NULL &&
        (entry->rights & required_rights) ==
            required_rights &&
        kernel_object_retain(
            entry->object
        )) {
        object = entry->object;
    }

    spinlock_unlock_irqrestore(
        &process_lock,
        interrupt_state
    );

    return object;
}

bool kernel_process_handle_close(
    struct kernel_process *process,
    uint64_t handle
)
{
    if (!process_system_initialized ||
        process == NULL ||
        handle >= KERNEL_PROCESS_MAX_HANDLES) {
        return false;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &process_lock
        );

    struct kernel_object *object = NULL;

    if (process_registered_locked(process) &&
        process->state ==
            KERNEL_PROCESS_ALIVE &&
        process->handles[handle].object !=
            NULL) {
        object =
            process->handles[handle].object;

        process->handles[handle].object =
            NULL;

        process->handles[handle].rights = 0;
    }

    spinlock_unlock_irqrestore(
        &process_lock,
        interrupt_state
    );

    if (object == NULL) {
        return false;
    }

    kernel_object_release(object);
    return true;
}

bool kernel_process_destroy(
    struct kernel_process *process
)
{
    if (!process_system_initialized ||
        process == NULL ||
        process == &kernel_process) {
        return false;
    }

    struct kernel_object *objects_to_release[
        KERNEL_PROCESS_MAX_HANDLES
    ];

    size_t release_count = 0;

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &process_lock
        );

    if (process->state !=
            KERNEL_PROCESS_ALIVE ||
        process->thread_count != 0) {
        spinlock_unlock_irqrestore(
            &process_lock,
            interrupt_state
        );

        return false;
    }

    struct kernel_process **link =
        &process_list;

    while (*link != NULL &&
           *link != process) {
        link = &(*link)->next;
    }

    if (*link != process) {
        spinlock_unlock_irqrestore(
            &process_lock,
            interrupt_state
        );

        return false;
    }

    /*
     * The address space must be empty before process destruction.
     */
    if (!vmm_address_space_destroy(
            &process->address_space
        )) {
        spinlock_unlock_irqrestore(
            &process_lock,
            interrupt_state
        );

        return false;
    }

    for (size_t index = 0;
         index < KERNEL_PROCESS_MAX_HANDLES;
         index++) {
        struct kernel_object *object =
            process->handles[index].object;

        if (object != NULL) {
            objects_to_release[release_count++] =
                object;

            process->handles[index].object =
                NULL;

            process->handles[index].rights = 0;
        }
    }

    *link = process->next;

    process->state =
        KERNEL_PROCESS_TERMINATED;

    spinlock_unlock_irqrestore(
        &process_lock,
        interrupt_state
    );

    /*
     * Destructors must run after releasing process_lock.
     */
    for (size_t index = 0;
         index < release_count;
         index++) {
        kernel_object_release(
            objects_to_release[index]
        );
    }

    return kfree(process);
}

struct kernel_process *kernel_process_kernel(void)
{
    if (!process_system_initialized) {
        return NULL;
    }

    return &kernel_process;
}

uint64_t kernel_process_id(
    const struct kernel_process *process
)
{
    return process != NULL
        ? process->id
        : UINT64_MAX;
}

enum kernel_process_state kernel_process_state(
    const struct kernel_process *process
)
{
    return process != NULL
        ? process->state
        : KERNEL_PROCESS_TERMINATED;
}

struct vmm_address_space *kernel_process_address_space(
    struct kernel_process *process
)
{
    if (process == NULL ||
        process->state !=
            KERNEL_PROCESS_ALIVE) {
        return NULL;
    }

    return &process->address_space;
}

size_t kernel_process_thread_count(
    const struct kernel_process *process
)
{
    return process != NULL
        ? process->thread_count
        : 0;
}

bool kernel_process_attach_thread(
    struct kernel_process *process
)
{
    if (!process_system_initialized ||
        process == NULL) {
        return false;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &process_lock
        );

    bool attached = false;

    if (process_registered_locked(process) &&
        process->state ==
            KERNEL_PROCESS_ALIVE &&
        process->thread_count <
            SIZE_MAX) {
        process->thread_count++;
        attached = true;
    }

    spinlock_unlock_irqrestore(
        &process_lock,
        interrupt_state
    );

    return attached;
}

bool kernel_process_detach_thread(
    struct kernel_process *process
)
{
    if (!process_system_initialized ||
        process == NULL) {
        return false;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &process_lock
        );

    bool detached = false;

    if (process_registered_locked(process) &&
        process->state ==
            KERNEL_PROCESS_ALIVE &&
        process->thread_count > 0) {
        process->thread_count--;
        detached = true;
    }

    spinlock_unlock_irqrestore(
        &process_lock,
        interrupt_state
    );

    return detached;
}

bool kernel_process_self_test(void)
{
    if (!process_system_initialized) {
        return false;
    }

    struct pmm_statistics before;
    struct pmm_statistics after;

    pmm_get_statistics(&before);

    struct kernel_process *process =
        kernel_process_create();

    if (process == NULL) {
        return false;
    }

    struct vmm_address_space *process_space =
        kernel_process_address_space(
            process
        );

    struct vmm_address_space *kernel_space =
        vmm_kernel_address_space();

    uint64_t test_frame =
        PMM_INVALID_ADDRESS;

    bool test_page_mapped = false;
    bool process_space_active = false;
    bool functional_test_passed = false;
    bool cleanup_passed = false;

    if (process_space == NULL ||
        kernel_space == NULL ||
        process->id == 0 ||
        process->state !=
            KERNEL_PROCESS_ALIVE ||
        process->thread_count != 0) {
        goto cleanup;
    }

    /*
     * Kernel code must resolve identically through both address
     * spaces.
     */
    uint64_t kernel_translation;
    uint64_t process_kernel_translation;

    if (!vmm_translate(
            kernel_space,
            (uint64_t)(uintptr_t)
                kernel_process_self_test,
            &kernel_translation
        ) ||
        !vmm_translate(
            process_space,
            (uint64_t)(uintptr_t)
                kernel_process_self_test,
            &process_kernel_translation
        ) ||
        kernel_translation !=
            process_kernel_translation) {
        goto cleanup;
    }

    /*
     * The process begins with no userspace mappings.
     */
    uint64_t ignored_physical;

    if (vmm_translate(
            process_space,
            USER_SPACE_BASE,
            &ignored_physical
        )) {
        goto cleanup;
    }

    test_frame =
        pmm_allocate_page();

    if (test_frame ==
        PMM_INVALID_ADDRESS) {
        goto cleanup;
    }

    void *test_frame_virtual =
        pmm_physical_to_virtual(
            test_frame
        );

    if (test_frame_virtual == NULL) {
        goto cleanup;
    }

    /*
     * Zero the frame before exposing it to userspace. Otherwise a
     * new process could observe data left by the kernel or another
     * process.
     */
    uint8_t *bytes =
        (uint8_t *)test_frame_virtual;

    for (size_t index = 0;
         index < VMM_PAGE_SIZE;
         index++) {
        bytes[index] = 0;
    }

    uint64_t user_flags =
        VMM_PAGE_USER |
        VMM_PAGE_WRITABLE;

    if (vmm_nx_supported()) {
        user_flags |=
            VMM_PAGE_NO_EXECUTE;
    }

    if (!vmm_map_page(
            process_space,
            USER_SPACE_BASE,
            test_frame,
            user_flags
        )) {
        goto cleanup;
    }

    test_page_mapped = true;

    uint64_t translated_frame;

    if (!vmm_translate(
            process_space,
            USER_SPACE_BASE,
            &translated_frame
        ) ||
        translated_frame !=
            test_frame) {
        goto cleanup;
    }

    /*
     * Mapping the page into the process must not create the same
     * mapping in PID 0.
     */
    if (vmm_translate(
            kernel_space,
            USER_SPACE_BASE,
            &ignored_physical
        )) {
        goto cleanup;
    }

    /*
     * Switch to the process hierarchy while still running in ring 0.
     * The shared upper-half mappings keep the kernel code and current
     * stack valid.
     */
    vmm_activate(process_space);

    if (!vmm_address_space_is_active(
            process_space
        )) {
        goto cleanup;
    }

    process_space_active = true;

    volatile uint64_t *test_value =
        (volatile uint64_t *)(uintptr_t)
            USER_SPACE_BASE;

    *test_value =
        UINT64_C(0x4B5253484E415053);

    if (*test_value !=
        UINT64_C(0x4B5253484E415053)) {
        goto cleanup;
    }

    vmm_activate(kernel_space);
    process_space_active = false;

    if (!vmm_address_space_is_active(
            kernel_space
        )) {
        goto cleanup;
    }

    uint64_t *physical_value =
        (uint64_t *)test_frame_virtual;

    if (*physical_value !=
        UINT64_C(0x4B5253484E415053)) {
        goto cleanup;
    }

    functional_test_passed = true;

cleanup:
    /*
     * Never attempt to destroy the currently active process space.
     */
    if (process_space_active) {
        vmm_activate(
            vmm_kernel_address_space()
        );

        process_space_active = false;
    }

    if (test_page_mapped) {
        uint64_t removed_frame;

        if (!vmm_unmap_page(
                process_space,
                USER_SPACE_BASE,
                &removed_frame
            ) ||
            removed_frame !=
                test_frame) {
            functional_test_passed = false;
        } else {
            test_page_mapped = false;
        }
    }

    if (!test_page_mapped &&
        test_frame !=
            PMM_INVALID_ADDRESS) {
        if (!pmm_free_page(
                test_frame
            )) {
            functional_test_passed = false;
        }

        test_frame =
            PMM_INVALID_ADDRESS;
    }

    if (!kernel_process_destroy(
            process
        )) {
        functional_test_passed = false;
    }

    pmm_get_statistics(&after);

    cleanup_passed =
        before.free_pages ==
            after.free_pages;

    return functional_test_passed &&
        cleanup_passed;
}