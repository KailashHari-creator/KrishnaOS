#ifndef KRISHNA_SYSCALL_H
#define KRISHNA_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#define KRISHNA_SYSCALL_VECTOR UINT8_C(0x80)
#define KRISHNA_SYSCALL_EXIT   UINT64_C(1)

bool syscall_init(void);

uint64_t *syscall_interrupt_dispatch(
    uint64_t *interrupted_rsp
);

uint64_t syscall_exit_count(void);
uint64_t syscall_last_exit_status(void);
uint64_t syscall_last_exit_process_id(void);

#endif