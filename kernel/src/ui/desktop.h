#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "graphics/graphics.h"

#define DESKTOP_WIDTH  1280
#define DESKTOP_HEIGHT 800

struct desktop {
    struct graphics_context *graphics;
    const uint8_t *wallpaper;

    uint64_t terminal_x;
    uint64_t terminal_y;
    uint64_t terminal_width;
    uint64_t terminal_height;
};

bool desktop_terminal_contains(
    const struct desktop *desktop,
    uint64_t x,
    uint64_t y
);

bool desktop_init(
    struct desktop *desktop,
    struct graphics_context *graphics,
    const uint8_t *wallpaper,
    uint64_t wallpaper_size
);

void desktop_render(
    struct desktop *desktop
);

static void desktop_draw_terminal_icon(
    struct desktop *desktop
);

bool desktop_terminal_contains(
    const struct desktop *desktop,
    uint64_t x,
    uint64_t y
);

#endif