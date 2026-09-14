#ifndef KRISHNA_ARCH_X86_64_CONTEXT_SWITCH_H
#define KRISHNA_ARCH_X86_64_CONTEXT_SWITCH_H

#include <stdint.h>

/*
 * Save the current callee-saved register context through old_rsp,
 * load new_rsp and resume the other context.
 */
void arch_context_switch(
    uint64_t **old_rsp,
    uint64_t *new_rsp
);

#endif