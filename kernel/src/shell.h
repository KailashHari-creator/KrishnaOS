#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>
#include <stdint.h>

#include "graphics/console.h"

#define SHELL_INPUT_CAPACITY 128

struct shell {
    struct console *console;

    char input[SHELL_INPUT_CAPACITY];
    size_t input_length;

    uint64_t usable_memory_bytes;
};

void shell_init(
    struct shell *shell,
    struct console *console,
    uint64_t usable_memory_bytes
);

void shell_handle_character(
    struct shell *shell,
    char character
);

#endif