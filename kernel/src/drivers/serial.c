#include "drivers/serial.h"

#define COM1 0x3F8

static void port_write(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static uint8_t port_read(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

void serial_init(void)
{
    /* Disable serial-port interrupts during configuration. */
    port_write(COM1 + 1, 0x00);

    /* Enable access to the baud-rate divisor. */
    port_write(COM1 + 3, 0x80);

    /* Divisor 3 gives 38,400 baud from the standard COM clock. */
    port_write(COM1 + 0, 0x03);
    port_write(COM1 + 1, 0x00);

    /* Eight data bits, one stop bit, no parity. */
    port_write(COM1 + 3, 0x03);

    /* Enable and clear the serial FIFO. */
    port_write(COM1 + 2, 0xC7);

    /* Enable the serial transmitter. */
    port_write(COM1 + 4, 0x0B);
}

void serial_write_character(char character)
{
    if (character == '\n') {
        while ((port_read(COM1 + 5) & 0x20) == 0) {
        }

        port_write(COM1, '\r');
    }

    while ((port_read(COM1 + 5) & 0x20) == 0) {
    }

    port_write(COM1, (uint8_t)character);
}

void serial_write(const char *text)
{
    while (*text != '\0') {
        serial_write_character(*text);
        text++;
    }
}

void serial_write_u64(uint64_t value)
{
    char digits[20];
    uint8_t count = 0;

    if (value == 0) {
        serial_write_character('0');
        return;
    }

    while (value != 0) {
        digits[count] = (char)('0' + value % 10);
        value /= 10;
        count++;
    }

    while (count != 0) {
        count--;
        serial_write_character(digits[count]);
    }
}

void serial_write_hex(uint64_t value)
{
    const char hexadecimal[] = "0123456789ABCDEF";

    serial_write("0x");

    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)((value >> shift) & 0xF);
        serial_write_character(hexadecimal[digit]);
    }
}