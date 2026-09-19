#ifndef KRISHNA_DESKTOP_FRONTEND_H
#define KRISHNA_DESKTOP_FRONTEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <krishna/graphics.h>

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

#endif