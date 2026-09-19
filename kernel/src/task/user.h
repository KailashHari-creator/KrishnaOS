#ifndef KRISHNA_TASK_USER_H
#define KRISHNA_TASK_USER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int64_t user_application_spawn(
    const char *path,
    size_t path_length
);

bool user_mode_self_test(void);
bool user_desktop_start(void);

#endif