#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * CPU-pushed x86-64 interrupt frame.
 *
 * RSP and SS are present when crossing privilege levels. Current
 * kernel-only interrupts use RIP, CS and RFLAGS.
 */
struct interrupt_frame {
    uint64_t instruction_pointer;
    uint64_t code_segment;
    uint64_t flags;
    uint64_t stack_pointer;
    uint64_t stack_segment;
} __attribute__((packed));

/*
 * Install fatal CPU exception handlers and load the IDT.
 *
 * External interrupts remain disabled.
 */
void interrupts_init(void);

/*
 * Install an interrupt gate into the already-loaded IDT.
 *
 * Call this while interrupts are disabled.
 */
bool interrupts_install_gate(
    uint8_t vector,
    uintptr_t handler_address
);

void interrupts_enable(void);
void interrupts_disable(void);

#endif