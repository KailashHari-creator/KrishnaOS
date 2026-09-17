#ifndef KRISHNA_ARCH_X86_64_CONTEXT_SWITCH_H
#define KRISHNA_ARCH_X86_64_CONTEXT_SWITCH_H

#include <stdint.h>

#define ARCH_RESCHEDULE_VECTOR UINT8_C(0xF1)

/*
 * Complete x86-64 interrupt-return context.
 *
 * The assembly entry saves the fifteen general-purpose registers.
 * The processor's 64-bit interrupt frame contains:
 *
 *     RIP, CS, RFLAGS, RSP, SS
 */
struct arch_interrupt_context {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t instruction_pointer;
    uint64_t code_segment;
    uint64_t flags;
    uint64_t stack_pointer;
    uint64_t stack_segment;
};

void arch_local_apic_timer_interrupt_entry(void);
void arch_reschedule_interrupt_entry(void);

void arch_request_context_switch(void);

#endif