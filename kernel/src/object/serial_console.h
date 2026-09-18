#ifndef KRISHNA_OBJECT_SERIAL_CONSOLE_H
#define KRISHNA_OBJECT_SERIAL_CONSOLE_H

#include <stdbool.h>

struct kernel_object;
struct kernel_process;

bool serial_console_object_init(void);

struct kernel_object *serial_console_object(void);

bool serial_console_attach_standard_handles(
    struct kernel_process *process
);

#endif