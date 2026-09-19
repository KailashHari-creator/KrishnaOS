#ifndef KRISHNA_USER_SYSCALL_H
#define KRISHNA_USER_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

int64_t krishna_syscall6(
    uint64_t syscall_number,
    uint64_t argument_1,
    uint64_t argument_2,
    uint64_t argument_3,
    uint64_t argument_4,
    uint64_t argument_5,
    uint64_t argument_6
);

#endif