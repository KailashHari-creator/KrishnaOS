#ifndef INTERRUPTS_H
#define INTERRUPTS_H

/*
 * Install handlers for fatal x86-64 CPU exceptions.
 *
 * This does not enable external hardware interrupts.
 */
void interrupts_init(void);

#endif