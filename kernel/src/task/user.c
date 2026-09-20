#include "task/user.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/layout.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "syscall.h"
#include "task/elf.h"
#include "task/process.h"
#include "task/thread.h"
#include "object/serial_console.h"
#include "object/input.h"
#include "object/framebuffer.h"
#include <krishna/abi.h>

/*
 * Place the userspace stack near the top of the lower canonical half.
 *
 * The mapped page covers:
 *
 *     USER_TEST_STACK_ADDRESS
 *         through
 *     USER_TEST_STACK_TOP - 1
 */
#define USER_TEST_STACK_ADDRESS \
    (USER_SPACE_TOP - VMM_PAGE_SIZE)

#define USER_TEST_STACK_TOP \
    USER_SPACE_TOP

#define USER_TEST_KERNEL_STACK_PAGES \
    ((size_t)4)

#define USER_TEST_EXIT_STATUS \
    UINT64_C(42)

/*
 * Defined by task/user_image.asm.
 */
extern const uint8_t embedded_user_test_elf_start[];
extern const uint8_t embedded_user_test_elf_end[];
extern const uint8_t embedded_desktop_elf_start[];
extern const uint8_t embedded_desktop_elf_end[];
extern const uint8_t embedded_terminal_elf_start[];
extern const uint8_t embedded_terminal_elf_end[];
extern const uint8_t embedded_venu_elf_start[];
extern const uint8_t embedded_venu_elf_end[];

#define USER_APPLICATION_STACK_PAGES ((size_t)16)
#define USER_APPLICATION_KERNEL_STACK_PAGES ((size_t)4)
#define USER_APPLICATION_RUNTIME_LIMIT ((size_t)16)

struct user_application_image {
    const char *path;
    size_t path_length;
    const uint8_t *start;
    const uint8_t *end;
};

struct user_application_runtime {
    bool occupied;
    struct kernel_process *process;
    struct elf64_loaded_image image;
    uintptr_t stack_physical;
    size_t stack_pages;
};

static struct user_application_runtime
    user_application_runtimes[
        USER_APPLICATION_RUNTIME_LIMIT
    ];

#define USER_DESKTOP_STACK_PAGES \
    ((size_t)16)

#define USER_DESKTOP_KERNEL_STACK_PAGES \
    ((size_t)4)

static bool desktop_started;

static struct {
    struct kernel_process *process;

    struct elf64_loaded_image image;

    uint64_t stack_physical;
    bool stack_mapped;
} desktop_runtime;

static bool user_path_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
)
{
    if (left == NULL ||
        right == NULL ||
        left_length != right_length) {
        return false;
    }

    for (size_t index = 0;
         index < left_length;
         index++) {
        if (left[index] != right[index]) {
            return false;
        }
    }

    return true;
}

static bool user_application_find(
    const char *path,
    size_t path_length,
    struct user_application_image *result
)
{
    static const char terminal_path[] =
        "/system/bin/terminal";

    static const char venu_path[] =
        "/system/bin/venu";

    if (result == NULL) {
        return false;
    }

    if (user_path_equal(
            path,
            path_length,
            terminal_path,
            sizeof(terminal_path) - 1
        )) {
        result->path = terminal_path;
        result->path_length =
            sizeof(terminal_path) - 1;
        result->start =
            embedded_terminal_elf_start;
        result->end =
            embedded_terminal_elf_end;
        return true;
    }

    if (user_path_equal(
            path,
            path_length,
            venu_path,
            sizeof(venu_path) - 1
        )) {
        result->path = venu_path;
        result->path_length =
            sizeof(venu_path) - 1;

        result->start =
            embedded_venu_elf_start;

        result->end =
            embedded_venu_elf_end;

        return true;
    }

    return false;
}

static struct user_application_runtime *
user_application_runtime_allocate(void)
{
    for (size_t index = 0;
         index < USER_APPLICATION_RUNTIME_LIMIT;
         index++) {
        if (!user_application_runtimes[index].occupied) {
            user_application_runtimes[index].occupied = true;
            return &user_application_runtimes[index];
        }
    }

    return NULL;
}

static void zero_page(void *page)
{
    uint8_t *bytes =
        (uint8_t *)page;

    for (size_t index = 0;
         index < VMM_PAGE_SIZE;
         index++) {
        bytes[index] = 0;
    }
}


bool user_mode_self_test(void)
{
    struct pmm_statistics before;
    struct pmm_statistics after;

    pmm_get_statistics(&before);

    uint64_t live_before =
        kernel_thread_live_count();

    uint64_t exits_before =
        syscall_exit_count();

    const uint8_t *elf_file =
        embedded_user_test_elf_start;

    size_t elf_size =
        (size_t)(
            (uintptr_t)embedded_user_test_elf_end -
            (uintptr_t)embedded_user_test_elf_start
        );

    /*
     * Validate the embedded file before creating any process resources.
     */
    if (elf_size == 0 ||
        !elf64_validate(
            elf_file,
            elf_size
        )) {
        return false;
    }

    struct kernel_process *process =
        kernel_process_create();

    struct elf64_loaded_image image = {
        .entry = 0,
        .pages = NULL,
        .page_count = 0
    };

    uint64_t stack_frame =
        PMM_INVALID_ADDRESS;

    bool image_loaded = false;
    bool stack_mapped = false;
    bool passed = false;

    if (process == NULL) {
        return false;
    }

    if (!serial_console_attach_standard_handles(
        process
    )) {
        (void)kernel_process_destroy(process);
        return false;
    }

    if (!input_objects_attach_standard_handles(
        process
    )) {
        (void)kernel_process_destroy(process);
        return false;
    }

    if (!framebuffer_object_attach(
        process
    )) {
        (void)kernel_process_destroy(process);
        return false;
    }

    uint64_t process_id =
        kernel_process_id(process);

    struct vmm_address_space *space =
        kernel_process_address_space(
            process
        );

    if (space == NULL) {
        goto cleanup;
    }

    /*
     * Parse the ELF and install its PT_LOAD segments into this process.
     */
    if (!elf64_load(
            process,
            elf_file,
            elf_size,
            &image
        )) {
        goto cleanup;
    }

    image_loaded = true;

    /*
     * Allocate a separate user stack.
     *
     * The ELF describes the program image, not the initial stack.
     */
    stack_frame =
        pmm_allocate_page();

    if (stack_frame ==
        PMM_INVALID_ADDRESS) {
        goto cleanup;
    }

    void *stack_memory =
        pmm_physical_to_virtual(
            stack_frame
        );

    if (stack_memory == NULL) {
        goto cleanup;
    }

    /*
     * A new process must never observe data left in an old physical
     * allocation.
     */
    zero_page(stack_memory);

    uint64_t stack_flags =
        VMM_PAGE_USER |
        VMM_PAGE_WRITABLE;

    if (vmm_nx_supported()) {
        stack_flags |=
            VMM_PAGE_NO_EXECUTE;
    }

    if (!vmm_map_page(
            space,
            USER_TEST_STACK_ADDRESS,
            stack_frame,
            stack_flags
        )) {
        goto cleanup;
    }

    stack_mapped = true;

    /*
     * The thread now begins at the entry address read from the ELF
     * header—not at a hard-coded address in the kernel.
     */
    struct kernel_thread *thread =
        kernel_thread_create_user(
            process,
            image.entry,
            USER_TEST_STACK_TOP,
            USER_TEST_KERNEL_STACK_PAGES
        );

    if (thread == NULL) {
        goto cleanup;
    }

    /*
     * The ELF program:
     *
     * 1. Uses CALL/RET on the user stack.
     * 2. Checks that BSS begins as zero.
     * 3. Writes to its RW data segment.
     * 4. Invokes SYS_EXIT through INT 0x80.
     */
    for (size_t attempt = 0;
         attempt < 32 &&
         kernel_thread_live_count() >
            live_before;
         attempt++) {
        kernel_thread_yield();
    }

    kernel_thread_preemption_point();

    /*
     * The user thread must have exited, released its process reference
     * and returned scheduling to PID 0.
     */
    if (kernel_thread_live_count() !=
            live_before ||
        kernel_process_thread_count(
            process
        ) != 0 ||
        kernel_thread_current_process_id() != 0 ||
        !vmm_address_space_is_active(
            vmm_kernel_address_space()
        )) {
        goto cleanup;
    }

    /*
     * A successful ELF program exits with status 42.
     *
     * It exits with status 99 if stack, data or BSS validation fails.
     */
    if (syscall_exit_count() !=
            exits_before + UINT64_C(1) ||
        syscall_last_exit_status() !=
            USER_TEST_EXIT_STATUS ||
        syscall_last_exit_process_id() !=
            process_id) {
        goto cleanup;
    }

    passed = true;

cleanup:
    /*
     * Never unmap memory that may still be used by a live thread.
     */
    if (kernel_thread_live_count() !=
        live_before) {
        return false;
    }

    kernel_thread_preemption_point();

    /*
     * Remove and release the user stack.
     */
    if (stack_mapped) {
        uint64_t removed_frame;

        if (!vmm_unmap_page(
                space,
                USER_TEST_STACK_ADDRESS,
                &removed_frame
            ) ||
            removed_frame !=
                stack_frame) {
            passed = false;
        } else {
            stack_mapped = false;
        }
    }

    if (!stack_mapped &&
        stack_frame !=
            PMM_INVALID_ADDRESS) {
        if (!pmm_free_page(
                stack_frame
            )) {
            passed = false;
        }

        stack_frame =
            PMM_INVALID_ADDRESS;
    }

    /*
     * Remove all pages installed from PT_LOAD segments.
     */
    if (image_loaded) {
        if (!elf64_unload(
                process,
                &image
            )) {
            passed = false;
        } else {
            image_loaded = false;
        }
    }

    /*
     * The process address space must be empty before destruction.
     */
    if (!stack_mapped &&
        !image_loaded &&
        !kernel_process_destroy(
            process
        )) {
        passed = false;
    }

    pmm_get_statistics(&after);

    /*
     * This catches leaked ELF frames, stack frames and page-table pages.
     */
    return passed &&
        before.free_pages ==
            after.free_pages;
}
int64_t user_application_spawn(
    const char *path,
    size_t path_length,
    struct kernel_process *parent_process,
    uint64_t inherited_handle,
    uint64_t child_handle
)
{
    if (path == NULL ||
        path_length == 0) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    if (inherited_handle !=
            KRISHNA_HANDLE_INVALID &&
        (
            parent_process == NULL ||
            child_handle >=
                KERNEL_PROCESS_MAX_HANDLES
        )) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    struct user_application_image application;

    if (!user_application_find(
            path,
            path_length,
            &application
        )) {
        return -KRISHNA_ERROR_NO_SUCH_FILE;
    }

    size_t elf_size =
        (size_t)(
            (uintptr_t)application.end -
            (uintptr_t)application.start
        );

    if (elf_size == 0 ||
        !elf64_validate(
            application.start,
            elf_size
        )) {
        return -KRISHNA_ERROR_EXEC_FORMAT;
    }

    struct user_application_runtime *runtime =
        user_application_runtime_allocate();

    if (runtime == NULL) {
        return -KRISHNA_ERROR_OUT_OF_MEMORY;
    }

    /*
     * From this point onward every failure must release the runtime
     * slot and every successfully allocated resource.
     */
    struct kernel_process *process = NULL;

    struct vmm_address_space *space = NULL;

    struct elf64_loaded_image image = {
        .entry = 0,
        .pages = NULL,
        .page_count = 0
    };

    uint64_t stack_physical =
        PMM_INVALID_ADDRESS;

    uint64_t stack_base =
        USER_SPACE_TOP -
        (uint64_t)
            USER_APPLICATION_STACK_PAGES *
            VMM_PAGE_SIZE;

    bool image_loaded = false;
    bool stack_mapped = false;

    int64_t failure_result =
        -KRISHNA_ERROR_OUT_OF_MEMORY;

    process = kernel_process_create();

    if (process == NULL) {
        goto failure;
    }

    space =
        kernel_process_address_space(
            process
        );

    if (space == NULL) {
        goto failure;
    }

    /*
     * Every normal application receives stdout and stderr.
     */
    if (!serial_console_attach_standard_handles(
            process
        )) {
        failure_result =
            -KRISHNA_ERROR_IO;

        goto failure;
    }

    /*
     * Optionally copy one parent handle into the child. For the
     * terminal this is the second endpoint of its IPC channel.
     */
    if (inherited_handle !=
        KRISHNA_HANDLE_INVALID) {
        if (!kernel_process_handle_duplicate(
                parent_process,
                inherited_handle,
                process,
                child_handle,
                KERNEL_HANDLE_RIGHT_READ |
                    KERNEL_HANDLE_RIGHT_WRITE
            )) {
            failure_result =
                -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR;

            goto failure;
        }
    }

    if (!elf64_load(
            process,
            application.start,
            elf_size,
            &image
        )) {
        failure_result =
            -KRISHNA_ERROR_EXEC_FORMAT;

        goto failure;
    }

    image_loaded = true;

    stack_physical =
        pmm_allocate_pages(
            USER_APPLICATION_STACK_PAGES
        );

    if (stack_physical ==
        PMM_INVALID_ADDRESS) {
        goto failure;
    }

    /*
     * Zero every physical stack page through the HHDM.
     */
    for (size_t page = 0;
         page <
            USER_APPLICATION_STACK_PAGES;
         page++) {
        void *page_memory =
            pmm_physical_to_virtual(
                stack_physical +
                (uint64_t)page *
                    VMM_PAGE_SIZE
            );

        if (page_memory == NULL) {
            goto failure;
        }

        zero_page(page_memory);
    }

    uint64_t stack_flags =
        VMM_PAGE_USER |
        VMM_PAGE_WRITABLE;

    if (vmm_nx_supported()) {
        stack_flags |=
            VMM_PAGE_NO_EXECUTE;
    }

    if (!vmm_map_pages(
            space,
            stack_base,
            stack_physical,
            USER_APPLICATION_STACK_PAGES,
            stack_flags
        )) {
        goto failure;
    }

    stack_mapped = true;

    uint64_t process_id =
        kernel_process_id(process);

    if (process_id == UINT64_MAX ||
        process_id > INT64_MAX) {
        failure_result =
            -KRISHNA_ERROR_NO_SUCH_PROCESS;

        goto failure;
    }

    /*
     * Save ownership information before the new thread becomes
     * runnable.
     */
    runtime->process = process;
    runtime->image = image;
    runtime->stack_physical =
        stack_physical;
    runtime->stack_pages =
        USER_APPLICATION_STACK_PAGES;

    struct kernel_thread *thread =
        kernel_thread_create_user(
            process,
            image.entry,
            USER_SPACE_TOP,
            USER_APPLICATION_KERNEL_STACK_PAGES
        );

    if (thread == NULL) {
        /*
         * The runtime record must not retain resources that the
         * failure path is about to release.
         */
        runtime->process = NULL;
        runtime->image =
            (struct elf64_loaded_image){
                .entry = 0,
                .pages = NULL,
                .page_count = 0
            };

        runtime->stack_physical =
            PMM_INVALID_ADDRESS;

        runtime->stack_pages = 0;

        goto failure;
    }

    return (int64_t)process_id;

failure:
    if (stack_mapped && space != NULL) {
        for (size_t page = 0;
             page <
                USER_APPLICATION_STACK_PAGES;
             page++) {
            (void)vmm_unmap_page(
                space,
                stack_base +
                    (uint64_t)page *
                        VMM_PAGE_SIZE,
                NULL
            );
        }
    }

    if (stack_physical !=
        PMM_INVALID_ADDRESS) {
        (void)pmm_free_pages(
            stack_physical,
            USER_APPLICATION_STACK_PAGES
        );
    }

    if (image_loaded &&
        process != NULL) {
        (void)elf64_unload(
            process,
            &image
        );
    }

    if (process != NULL) {
        (void)kernel_process_destroy(
            process
        );
    }

    runtime->occupied = false;
    runtime->process = NULL;

    runtime->image =
        (struct elf64_loaded_image){
            .entry = 0,
            .pages = NULL,
            .page_count = 0
        };

    runtime->stack_physical =
        PMM_INVALID_ADDRESS;

    runtime->stack_pages = 0;

    return failure_result;
}
bool user_desktop_start(void)
{
    if (desktop_started) {
        return false;
    }

    const uint8_t *elf_file =
        embedded_desktop_elf_start;

    size_t elf_size =
        (size_t)(
            (uintptr_t)embedded_desktop_elf_end -
            (uintptr_t)embedded_desktop_elf_start
        );

    if (elf_size == 0 ||
        !elf64_validate(
            elf_file,
            elf_size
        )) {
        return false;
    }

    struct kernel_process *process =
        kernel_process_create();

    if (process == NULL) {
        return false;
    }

    struct elf64_loaded_image image = {
        .entry = 0,
        .pages = NULL,
        .page_count = 0
    };

    uint64_t stack_physical =
        PMM_INVALID_ADDRESS;

    bool image_loaded = false;
    bool stack_mapped = false;

    struct vmm_address_space *space =
        kernel_process_address_space(
            process
        );

    if (space == NULL) {
        goto failure;
    }

    if (!serial_console_attach_standard_handles(
            process
        ) ||
        !input_objects_attach_standard_handles(
            process
        ) ||
        !framebuffer_object_attach(
            process
        )) {
        goto failure;
    }

    if (!elf64_load(
            process,
            elf_file,
            elf_size,
            &image
        )) {
        goto failure;
    }

    image_loaded = true;

    stack_physical =
        pmm_allocate_pages(
            USER_DESKTOP_STACK_PAGES
        );

    if (stack_physical ==
        PMM_INVALID_ADDRESS) {
        goto failure;
    }

    for (size_t page = 0;
         page < USER_DESKTOP_STACK_PAGES;
         page++) {
        void *memory =
            pmm_physical_to_virtual(
                stack_physical +
                (uint64_t)page *
                    VMM_PAGE_SIZE
            );

        if (memory == NULL) {
            goto failure;
        }

        zero_page(memory);
    }

    uint64_t stack_base =
        USER_SPACE_TOP -
        (uint64_t)
            USER_DESKTOP_STACK_PAGES *
            VMM_PAGE_SIZE;

    uint64_t stack_flags =
        VMM_PAGE_USER |
        VMM_PAGE_WRITABLE;

    if (vmm_nx_supported()) {
        stack_flags |=
            VMM_PAGE_NO_EXECUTE;
    }

    if (!vmm_map_pages(
            space,
            stack_base,
            stack_physical,
            USER_DESKTOP_STACK_PAGES,
            stack_flags
        )) {
        goto failure;
    }

    stack_mapped = true;

    /*
     * Preserve ownership information before making the thread runnable.
     */
    desktop_runtime.process = process;
    desktop_runtime.image = image;
    desktop_runtime.stack_physical =
        stack_physical;

    desktop_runtime.stack_mapped =
        true;

    struct kernel_thread *thread =
        kernel_thread_create_user(
            process,
            image.entry,
            USER_SPACE_TOP,
            USER_DESKTOP_KERNEL_STACK_PAGES
        );

    if (thread == NULL) {
        desktop_runtime =
            (typeof(desktop_runtime)){0};

        goto failure;
    }

    desktop_started = true;
    return true;

failure:
    if (stack_mapped) {
        uint64_t stack_base =
            USER_SPACE_TOP -
            (uint64_t)
                USER_DESKTOP_STACK_PAGES *
                VMM_PAGE_SIZE;

        for (size_t page = 0;
             page <
                USER_DESKTOP_STACK_PAGES;
             page++) {
            (void)vmm_unmap_page(
                space,
                stack_base +
                    (uint64_t)page *
                        VMM_PAGE_SIZE,
                NULL
            );
        }
    }

    if (stack_physical !=
        PMM_INVALID_ADDRESS) {
        (void)pmm_free_pages(
            stack_physical,
            USER_DESKTOP_STACK_PAGES
        );
    }

    if (image_loaded) {
        (void)elf64_unload(
            process,
            &image
        );
    }

    (void)kernel_process_destroy(
        process
    );

    return false;
}