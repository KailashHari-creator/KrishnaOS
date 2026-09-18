#include "object/input.h"

#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>

#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "object/object.h"
#include "task/process.h"

static struct kernel_object keyboard_object;
static struct kernel_object mouse_object;

static bool input_initialized;

_Static_assert(
    sizeof(struct krishna_keyboard_event) == 4,
    "Keyboard ABI event size changed"
);

_Static_assert(
    sizeof(struct krishna_mouse_event) == 8,
    "Mouse ABI event size changed"
);

static int64_t keyboard_object_read(
    void *context,
    void *buffer,
    size_t size
)
{
    (void)context;

    if (buffer == NULL ||
        size <
            sizeof(
                struct krishna_keyboard_event
            )) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    struct krishna_keyboard_event *events =
        (struct krishna_keyboard_event *)
            buffer;

    size_t capacity =
        size /
        sizeof(struct krishna_keyboard_event);

    size_t count = 0;

    while (count < capacity) {
        struct key_event event;

        if (!keyboard_poll(&event)) {
            break;
        }

        events[count].scancode =
            event.scancode;

        events[count].character =
            (uint8_t)event.character;

        events[count].pressed =
            event.pressed
                ? UINT8_C(1)
                : UINT8_C(0);

        events[count].reserved = 0;

        count++;
    }

    if (count == 0) {
        return -KRISHNA_ERROR_WOULD_BLOCK;
    }

    return (int64_t)(
        count *
        sizeof(struct krishna_keyboard_event)
    );
}

static int64_t mouse_object_read(
    void *context,
    void *buffer,
    size_t size
)
{
    (void)context;

    if (buffer == NULL ||
        size <
            sizeof(
                struct krishna_mouse_event
            )) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    struct krishna_mouse_event *events =
        (struct krishna_mouse_event *)
            buffer;

    size_t capacity =
        size /
        sizeof(struct krishna_mouse_event);

    size_t count = 0;

    while (count < capacity) {
        struct mouse_event event;

        if (!mouse_poll(&event)) {
            break;
        }

        uint8_t buttons = 0;

        if (event.left_button) {
            buttons |=
                KRISHNA_MOUSE_BUTTON_LEFT;
        }

        if (event.right_button) {
            buttons |=
                KRISHNA_MOUSE_BUTTON_RIGHT;
        }

        if (event.middle_button) {
            buttons |=
                KRISHNA_MOUSE_BUTTON_MIDDLE;
        }

        events[count].delta_x =
            event.delta_x;

        events[count].delta_y =
            event.delta_y;

        events[count].buttons =
            buttons;

        events[count].reserved[0] = 0;
        events[count].reserved[1] = 0;
        events[count].reserved[2] = 0;

        count++;
    }

    if (count == 0) {
        return -KRISHNA_ERROR_WOULD_BLOCK;
    }

    return (int64_t)(
        count *
        sizeof(struct krishna_mouse_event)
    );
}

static const struct kernel_object_operations
keyboard_operations = {
    .read = keyboard_object_read,
    .write = NULL,
    .destroy = NULL
};

static const struct kernel_object_operations
mouse_operations = {
    .read = mouse_object_read,
    .write = NULL,
    .destroy = NULL
};

bool input_objects_init(void)
{
    if (input_initialized) {
        return true;
    }

    if (!kernel_object_initialize(
            &keyboard_object,
            &keyboard_operations,
            NULL
        )) {
        return false;
    }

    if (!kernel_object_initialize(
            &mouse_object,
            &mouse_operations,
            NULL
        )) {
        return false;
    }

    input_initialized = true;
    return true;
}

bool input_objects_attach_standard_handles(
    struct kernel_process *process
)
{
    if (!input_initialized ||
        process == NULL) {
        return false;
    }

    if (!kernel_process_handle_install(
            process,
            KRISHNA_HANDLE_KEYBOARD,
            &keyboard_object,
            KERNEL_HANDLE_RIGHT_READ
        )) {
        return false;
    }

    if (!kernel_process_handle_install(
            process,
            KRISHNA_HANDLE_MOUSE,
            &mouse_object,
            KERNEL_HANDLE_RIGHT_READ
        )) {
        (void)kernel_process_handle_close(
            process,
            KRISHNA_HANDLE_KEYBOARD
        );

        return false;
    }

    return true;
}