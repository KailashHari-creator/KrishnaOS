#ifndef X86_64_IO_H
#define X86_64_IO_H

#include <stdint.h>

static inline void io_write8(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint8_t io_read8(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

/*
 * Give legacy hardware enough time to process the previous I/O
 * operation. Port 0x80 is conventionally used for this delay.
 */
static inline void io_wait(void)
{
    io_write8(0x80, 0);
}

#endif