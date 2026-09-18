#include "task/user.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/layout.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "syscall.h"
#include "task/process.h"
#include "task/thread.h"

#define USER_TEST_CODE_ADDRESS \
    USER_SPACE_BASE

#define USER_TEST_STACK_ADDRESS \
    (USER_SPACE_BASE + VMM_PAGE_SIZE)

#define USER_TEST_STACK_TOP \
    (USER_TEST_STACK_ADDRESS + VMM_PAGE_SIZE)

#define USER_TEST_KERNEL_STACK_PAGES \
    ((size_t)4)

#define USER_TEST_EXIT_STATUS \
    UINT64_C(42)

/*
 * Ring-3 program:
 *
 *     mov eax, 1       ; SYS_EXIT
 *     mov edi, 42      ; exit status
 *     int 0x80
 *     ud2              ; must never execute
 */
static const uint8_t user_test_program[] = {
    UINT8_C(0xB8),
    UINT8_C(0x01), UINT8_C(0x00),
    UINT8_C(0x00), UINT8_C(0x00),

    UINT8_C(0xBF),
    UINT8_C(0x2A), UINT8_C(0x00),
    UINT8_C(0x00), UINT8_C(0x00),

    UINT8_C(0xCD), UINT8_C(0x80),
    UINT8_C(0x0F), UINT8_C(0x0B)
};

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

    struct kernel_process *process =
        kernel_process_create();

    uint64_t code_frame =
        PMM_INVALID_ADDRESS;

    uint64_t stack_frame =
        PMM_INVALID_ADDRESS;

    bool code_mapped = false;
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

    code_frame = pmm_allocate_page();
    stack_frame = pmm_allocate_page();

    if (code_frame == PMM_INVALID_ADDRESS ||
        stack_frame == PMM_INVALID_ADDRESS) {
        goto cleanup;
    }

    uint8_t *code_memory =
        (uint8_t *)pmm_physical_to_virtual(
            code_frame
        );

    void *stack_memory =
        pmm_physical_to_virtual(
            stack_frame
        );

    if (code_memory == NULL ||
        stack_memory == NULL) {
        goto cleanup;
    }

    zero_page(code_memory);
    zero_page(stack_memory);

    for (size_t index = 0;
         index < sizeof(user_test_program);
         index++) {
        code_memory[index] =
            user_test_program[index];
    }

    /*
     * Executable and user-accessible, but not writable.
     */
    if (!vmm_map_page(
            space,
            USER_TEST_CODE_ADDRESS,
            code_frame,
            VMM_PAGE_USER
        )) {
        goto cleanup;
    }

    code_mapped = true;

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

    struct kernel_thread *thread =
        kernel_thread_create_user(
            process,
            USER_TEST_CODE_ADDRESS,
            USER_TEST_STACK_TOP,
            USER_TEST_KERNEL_STACK_PAGES
        );

    if (thread == NULL) {
        goto cleanup;
    }

    /*
     * The test program should execute and exit during the first
     * scheduling opportunity.
     */
    for (size_t attempt = 0;
         attempt < 32 &&
         kernel_thread_live_count() > live_before;
         attempt++) {
        kernel_thread_yield();
    }

    kernel_thread_preemption_point();

    if (kernel_thread_live_count() != live_before ||
        kernel_process_thread_count(process) != 0 ||
        kernel_thread_current_process_id() != 0 ||
        !vmm_address_space_is_active(
            vmm_kernel_address_space()
        )) {
        goto cleanup;
    }

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
     * Never remove mappings belonging to a running thread.
     */
    if (kernel_thread_live_count() !=
        live_before) {
        return false;
    }

    kernel_thread_preemption_point();

    if (stack_mapped) {
        uint64_t removed;

        if (!vmm_unmap_page(
                space,
                USER_TEST_STACK_ADDRESS,
                &removed
            ) ||
            removed != stack_frame) {
            passed = false;
        } else {
            stack_mapped = false;
        }
    }

    if (code_mapped) {
        uint64_t removed;

        if (!vmm_unmap_page(
                space,
                USER_TEST_CODE_ADDRESS,
                &removed
            ) ||
            removed != code_frame) {
            passed = false;
        } else {
            code_mapped = false;
        }
    }

    if (!stack_mapped &&
        stack_frame != PMM_INVALID_ADDRESS) {
        if (!pmm_free_page(stack_frame)) {
            passed = false;
        }

        stack_frame = PMM_INVALID_ADDRESS;
    }

    if (!code_mapped &&
        code_frame != PMM_INVALID_ADDRESS) {
        if (!pmm_free_page(code_frame)) {
            passed = false;
        }

        code_frame = PMM_INVALID_ADDRESS;
    }

    if (!code_mapped &&
        !stack_mapped &&
        !kernel_process_destroy(process)) {
        passed = false;
    }

    pmm_get_statistics(&after);

    return passed &&
        before.free_pages == after.free_pages;
}