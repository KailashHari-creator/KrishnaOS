#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>
#include <krishna/io.h>
#include <krishna/process.h>
#include <krishna/syscall.h>
#include <krishna/device.h>
#include <krishna/memory.h>

#define EXPECTED_EXIT_STATUS 42
#define FAILURE_EXIT_STATUS  99

/*
 * These retain the ELF loader's data and BSS checks.
 */
static volatile uint64_t initialized_data =
    UINT64_C(0x1122334455667788);

static volatile uint64_t zero_initialized_data;

int main(void)
{
    static const char message[] =
        "[USER] Hello from a KRISHNA OS C application!\n";

    if (zero_initialized_data != 0) {
        return FAILURE_EXIT_STATUS;
    }

    initialized_data =
        UINT64_C(0x4B524953484E414F);

    zero_initialized_data =
        initialized_data;

    if (initialized_data !=
            UINT64_C(0x4B524953484E414F) ||
        zero_initialized_data !=
            UINT64_C(0x4B524953484E414F)) {
        return FAILURE_EXIT_STATUS;
    }

    int64_t process_id =
        krishna_getpid();

    if (process_id <= 0) {
        return FAILURE_EXIT_STATUS;
    }

    int64_t written =
        krishna_write(
            KRISHNA_STDOUT,
            message,
            sizeof(message) - 1
        );

    if (written !=
        (int64_t)(sizeof(message) - 1)) {
        return FAILURE_EXIT_STATUS;
    }

    /*
     * Verify that an unmapped low address is rejected rather than
     * crashing or allowing the kernel to read it.
     */
    int64_t invalid_pointer_result =
        krishna_write(
            KRISHNA_STDOUT,
            (const void *)(uintptr_t)1,
            1
        );

    if (invalid_pointer_result !=
        -KRISHNA_ERROR_ACCESS_FAULT) {
        return FAILURE_EXIT_STATUS;
    }

        int64_t invalid_handle_result =
        krishna_write(
            UINT64_C(63),
            message,
            1
        );

    if (invalid_handle_result !=
        -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR) {
        return FAILURE_EXIT_STATUS;
    }

    if (krishna_close(
            KRISHNA_STDERR
        ) != 0) {
        return FAILURE_EXIT_STATUS;
    }

    int64_t closed_handle_result =
        krishna_write(
            KRISHNA_STDERR,
            message,
            1
        );

    if (closed_handle_result !=
        -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR) {
        return FAILURE_EXIT_STATUS;
    }

    /*
     * Verify the table-driven unknown-syscall path.
     */
    int64_t unknown_result =
        krishna_syscall6(
            UINT64_C(999),
            0,
            0,
            0,
            0,
            0,
            0
        );

    if (unknown_result !=
        -KRISHNA_ERROR_NOT_IMPLEMENTED) {
        return FAILURE_EXIT_STATUS;
    }

        struct krishna_keyboard_event
        keyboard_event;

    int64_t keyboard_result =
        krishna_read(
            KRISHNA_HANDLE_KEYBOARD,
            &keyboard_event,
            sizeof(keyboard_event)
        );

    if (keyboard_result !=
            (int64_t)sizeof(keyboard_event) &&
        keyboard_result !=
            -KRISHNA_ERROR_WOULD_BLOCK) {
        return FAILURE_EXIT_STATUS;
    }

    struct krishna_mouse_event
        mouse_event;

    int64_t mouse_result =
        krishna_read(
            KRISHNA_HANDLE_MOUSE,
            &mouse_event,
            sizeof(mouse_event)
        );

    if (mouse_result !=
            (int64_t)sizeof(mouse_event) &&
        mouse_result !=
            -KRISHNA_ERROR_WOULD_BLOCK) {
        return FAILURE_EXIT_STATUS;
    }

    /*
     * An invalid destination must fail before consuming an event.
     */
    int64_t invalid_read_result =
        krishna_read(
            KRISHNA_HANDLE_KEYBOARD,
            (void *)(uintptr_t)1,
            sizeof(keyboard_event)
        );

    if (invalid_read_result !=
        -KRISHNA_ERROR_ACCESS_FAULT) {
        return FAILURE_EXIT_STATUS;
    }

        struct krishna_framebuffer_info
        framebuffer_info;

    int64_t framebuffer_info_result =
        krishna_ioctl(
            KRISHNA_HANDLE_FRAMEBUFFER,
            KRISHNA_FRAMEBUFFER_IOCTL_GET_INFO,
            &framebuffer_info,
            sizeof(framebuffer_info)
        );

    if (framebuffer_info_result !=
        (int64_t)sizeof(framebuffer_info)) {
        return FAILURE_EXIT_STATUS;
    }

    int64_t framebuffer_mapping =
        krishna_memory_map(
            KRISHNA_HANDLE_FRAMEBUFFER,
            NULL,
            (size_t)
                framebuffer_info.byte_size,
            0,
            KRISHNA_MEMORY_PROTECTION_READ |
                KRISHNA_MEMORY_PROTECTION_WRITE,
            0
        );

    if (framebuffer_mapping < 0) {
        return FAILURE_EXIT_STATUS;
    }

    volatile uint8_t *framebuffer_bytes =
        (volatile uint8_t *)(uintptr_t)
            framebuffer_mapping;

    uint32_t test_colour =
        (UINT32_C(255) <<
            framebuffer_info.red_mask_shift) |
        (UINT32_C(32) <<
            framebuffer_info.green_mask_shift) |
        (UINT32_C(180) <<
            framebuffer_info.blue_mask_shift);

    /*
     * Draw a 64×64 magenta marker in the upper-left corner.
     * This is the first framebuffer rendering performed from Ring 3.
     */
    uint64_t test_width =
        framebuffer_info.width < 64
            ? framebuffer_info.width
            : 64;

    uint64_t test_height =
        framebuffer_info.height < 64
            ? framebuffer_info.height
            : 64;

    for (uint64_t y = 0;
         y < test_height;
         y++) {
        volatile uint32_t *row =
            (volatile uint32_t *)
            (void *)(
                framebuffer_bytes +
                y * framebuffer_info.pitch
            );

        for (uint64_t x = 0;
             x < test_width;
             x++) {
            row[x] = test_colour;
        }
    }

    if (krishna_memory_unmap(
            KRISHNA_HANDLE_FRAMEBUFFER,
            (void *)(uintptr_t)
                framebuffer_mapping,
            (size_t)
                framebuffer_info.byte_size
        ) != 0) {
        return FAILURE_EXIT_STATUS;
    }

    if (krishna_yield() != 0) {
        return FAILURE_EXIT_STATUS;
    }

    return EXPECTED_EXIT_STATUS;
}