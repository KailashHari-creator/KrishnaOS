#ifndef KRISHNA_DESKTOP_FRONTEND_H
#define KRISHNA_DESKTOP_FRONTEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <krishna/graphics.h>
#include <krishna/abi.h>

#define DESKTOP_TERMINAL_INPUT_CAPACITY 256
#define DESKTOP_TERMINAL_SCROLLBACK_LINES 64
#define DESKTOP_TERMINAL_LINE_CAPACITY    96

enum desktop_frontend_action {
    DESKTOP_FRONTEND_ACTION_NONE,
    DESKTOP_FRONTEND_ACTION_OPEN_TERMINAL,
    DESKTOP_FRONTEND_ACTION_CLOSE_TERMINAL
};

struct desktop_font {
    const uint8_t *glyphs;

    uint16_t glyph_width;
    uint16_t glyph_height;
    uint16_t first_character;
    uint16_t glyph_count;
};

struct desktop_frontend {
    struct graphics_context *graphics;

    struct desktop_font font;

    const uint8_t *wallpaper;
    const uint8_t *terminal_icon;

    uint64_t terminal_icon_x;
    uint64_t terminal_icon_y;

    bool terminal_hovered;
    bool terminal_open;

    char terminal_input[
        DESKTOP_TERMINAL_INPUT_CAPACITY
    ];

    size_t terminal_input_length;

        char terminal_lines[
        DESKTOP_TERMINAL_SCROLLBACK_LINES
    ][
        DESKTOP_TERMINAL_LINE_CAPACITY + 1
    ];

    size_t terminal_line_count;

    char terminal_partial[
        DESKTOP_TERMINAL_LINE_CAPACITY + 1
    ];

    size_t terminal_partial_length;

    bool terminal_ready;
    bool terminal_show_art;
};

bool desktop_frontend_initialize(
    struct desktop_frontend *desktop,
    struct graphics_context *graphics
);

void desktop_frontend_render(
    struct desktop_frontend *desktop
);

bool desktop_frontend_update_pointer(
    struct desktop_frontend *desktop,
    uint64_t x,
    uint64_t y
);

enum desktop_frontend_action
desktop_frontend_click(
    struct desktop_frontend *desktop,
    uint64_t x,
    uint64_t y
);

bool desktop_frontend_close_terminal(
    struct desktop_frontend *desktop
);

bool desktop_frontend_handle_key(
    struct desktop_frontend *desktop,
    const struct krishna_keyboard_event *event
);

bool desktop_frontend_set_terminal_input(
    struct desktop_frontend *desktop,
    const char *text,
    size_t length
);

bool desktop_frontend_append_terminal_output(
    struct desktop_frontend *desktop,
    const char *text,
    size_t length
);

bool desktop_frontend_clear_terminal(
    struct desktop_frontend *desktop
);

bool desktop_frontend_set_terminal_ready(
    struct desktop_frontend *desktop,
    bool ready
);

#endif