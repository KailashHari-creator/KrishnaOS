#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_write(const char *text);
void serial_write_u64(uint64_t value);
void serial_write_hex(uint64_t value);
void serial_write_character(char character);

#endif