#include <stdint.h>
#include <stddef.h>
#ifndef KRISHNA_ABI_H
#define KRISHNA_ABI_H

/*
 * KRISHNA OS userspace ABI version.
 */
#define KRISHNA_ABI_VERSION 0


/*
 * System-call numbers.
 *
 * Never renumber an existing released syscall. New operations should be
 * appended so previously compiled applications continue to work.
 */
#define KRISHNA_SYSCALL_EXIT             1
#define KRISHNA_SYSCALL_WRITE            2
#define KRISHNA_SYSCALL_READ             3
#define KRISHNA_SYSCALL_OPEN             4
#define KRISHNA_SYSCALL_CLOSE            5
#define KRISHNA_SYSCALL_SEEK             6
#define KRISHNA_SYSCALL_STAT             7
#define KRISHNA_SYSCALL_GETPID           8
#define KRISHNA_SYSCALL_YIELD            9
#define KRISHNA_SYSCALL_SLEEP           10
#define KRISHNA_SYSCALL_CLOCK_GET       11
#define KRISHNA_SYSCALL_MEMORY_MAP      12
#define KRISHNA_SYSCALL_MEMORY_UNMAP    13
#define KRISHNA_SYSCALL_MEMORY_PROTECT  14
#define KRISHNA_SYSCALL_PROCESS_SPAWN   15
#define KRISHNA_SYSCALL_PROCESS_WAIT    16
#define KRISHNA_SYSCALL_PROCESS_KILL    17
#define KRISHNA_SYSCALL_CHANNEL_CREATE  18
#define KRISHNA_SYSCALL_CHANNEL_SEND    19
#define KRISHNA_SYSCALL_CHANNEL_RECEIVE 20
#define KRISHNA_SYSCALL_POLL            21
#define KRISHNA_SYSCALL_IOCTL           22

#define KRISHNA_SYSCALL_COUNT           23

/*
 * Standard handles.
 */
#define KRISHNA_STDIN   0
#define KRISHNA_STDOUT  1
#define KRISHNA_STDERR  2
#define KRISHNA_HANDLE_KEYBOARD 3
#define KRISHNA_HANDLE_MOUSE    4
/*
 * Positive error numbers.
 *
 * Kernel syscall returns use their negative forms:
 *
 *     -KRISHNA_ERROR_INVALID_ARGUMENT
 */
#define KRISHNA_ERROR_PERMISSION_DENIED    1
#define KRISHNA_ERROR_NO_SUCH_PROCESS      3
#define KRISHNA_ERROR_INTERRUPTED          4
#define KRISHNA_ERROR_IO                   5
#define KRISHNA_ERROR_BAD_FILE_DESCRIPTOR  9
#define KRISHNA_ERROR_WOULD_BLOCK         11
#define KRISHNA_ERROR_OUT_OF_MEMORY       12
#define KRISHNA_ERROR_ACCESS_FAULT        14
#define KRISHNA_ERROR_BUSY                16
#define KRISHNA_ERROR_EXISTS              17
#define KRISHNA_ERROR_NO_SUCH_FILE         2
#define KRISHNA_ERROR_INVALID_ARGUMENT    22
#define KRISHNA_ERROR_NOT_IMPLEMENTED     38

/*
 * Keyboard events returned by reading KRISHNA_HANDLE_KEYBOARD.
 */
struct krishna_keyboard_event {
    uint8_t scancode;
    uint8_t character;
    uint8_t pressed;
    uint8_t reserved;
};

/*
 * Mouse button bits.
 */
#define KRISHNA_MOUSE_BUTTON_LEFT   (UINT8_C(1) << 0)
#define KRISHNA_MOUSE_BUTTON_RIGHT  (UINT8_C(1) << 1)
#define KRISHNA_MOUSE_BUTTON_MIDDLE (UINT8_C(1) << 2)

/*
 * Mouse events returned by reading KRISHNA_HANDLE_MOUSE.
 */
struct krishna_mouse_event {
    int16_t delta_x;
    int16_t delta_y;
    uint8_t buttons;
    uint8_t reserved[3];
};

#define KRISHNA_HANDLE_FRAMEBUFFER 5

#define KRISHNA_FRAMEBUFFER_IOCTL_GET_INFO \
    UINT64_C(0x4600)

#define KRISHNA_MEMORY_PROTECTION_READ \
    (UINT64_C(1) << 0)

#define KRISHNA_MEMORY_PROTECTION_WRITE \
    (UINT64_C(1) << 1)

struct krishna_framebuffer_info {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint64_t byte_size;

    uint32_t bits_per_pixel;

    uint8_t red_mask_shift;
    uint8_t red_mask_size;

    uint8_t green_mask_shift;
    uint8_t green_mask_size;

    uint8_t blue_mask_shift;
    uint8_t blue_mask_size;

    uint8_t reserved[6];
};

#endif