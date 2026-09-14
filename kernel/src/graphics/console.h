#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graphics/graphics.h"

struct console {
    struct graphics_context *graphics;

    const uint8_t *glyph_data;
    uint16_t glyph_width;
    uint16_t glyph_height;
    uint16_t first_character;
    uint16_t glyph_count;

    uint64_t cursor_x;
    uint64_t cursor_y;
    uint64_t margin;
    uint64_t spacing;
    uint64_t line_height;

    uint32_t foreground;
    uint32_t background;

    uint32_t cursor_colour;
    bool cursor_visible;
};

bool console_init(
    struct console *console,
    struct graphics_context *graphics,
    const uint8_t *font_file,
    size_t font_file_size
);

void console_clear(struct console *console);
void console_put_character(struct console *console, char character);
void console_write(struct console *console, const char *text);
void console_write_u64(struct console *console, uint64_t value);
static void console_hide_cursor(struct console *console);
static void console_show_cursor(struct console *console);
void console_set_cursor_visible(
    struct console *console,
    bool visible
);
void console_clear(struct console *console);
#endif