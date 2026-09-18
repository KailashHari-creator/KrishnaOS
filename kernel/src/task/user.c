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