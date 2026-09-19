#ifndef KRISHNA_USER_PROCESS_H
#define KRISHNA_USER_PROCESS_H

#include <stdint.h>

_Noreturn void krishna_exit(
    int64_t status
);

int64_t krishna_sleep(
    uint64_t milliseconds
);

int64_t krishna_getpid(void);
int64_t krishna_yield(void);

#endif