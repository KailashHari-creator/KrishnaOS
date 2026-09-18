#ifndef KRISHNA_OBJECT_INPUT_H
#define KRISHNA_OBJECT_INPUT_H

#include <stdbool.h>

struct kernel_process;

bool input_objects_init(void);

bool input_objects_attach_standard_handles(
    struct kernel_process *process
);

#endif