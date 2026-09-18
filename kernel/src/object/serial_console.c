#include "object/serial_console.h"

#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>

#include "drivers/serial.h"
#include "object/object.h"
#include "task/process.h"

static struct kernel_object console_object;
static bool console_initialized;

static int64_t serial_console_write(
    void *context,
    const void *buffer,
    size_t size
)
{
    (void)context;

    if (size != 0 && buffer == NULL) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    if (size > INT64_MAX) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    const char *characters =
        (const char *)buffer;

    for (size_t index = 0;
         index < size;
         index++) {
        serial_write_character(
            characters[index]
        );
    }

    return (int64_t)size;
}

static const struct kernel_object_operations
console_operations = {
    .read = NULL,
    .write = serial_console_write,
    .destroy = NULL
};

bool serial_console_object_init(void)
{
    if (console_initialized) {
        return true;
    }

    if (!kernel_object_initialize(
            &console_object,
            &console_operations,
            NULL
        )) {
        return false;
    }

    console_initialized = true;
    return true;
}

struct kernel_object *serial_console_object(void)
{
    return console_initialized
        ? &console_object
        : NULL;
}

bool serial_console_attach_standard_handles(
    struct kernel_process *process
)
{
    struct kernel_object *object =
        serial_console_object();

    if (process == NULL ||
        object == NULL) {
        return false;
    }

    if (!kernel_process_handle_install(
            process,
            KRISHNA_STDOUT,
            object,
            KERNEL_HANDLE_RIGHT_WRITE
        )) {
        return false;
    }

    if (!kernel_process_handle_install(
            process,
            KRISHNA_STDERR,
            object,
            KERNEL_HANDLE_RIGHT_WRITE
        )) {
        (void)kernel_process_handle_close(
            process,
            KRISHNA_STDOUT
        );

        return false;
    }

    return true;
}