#ifndef MOUSE_CURSOR_H
#define MOUSE_CURSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "graphics/graphics.h"

#define MOUSE_CURSOR_WIDTH  20
#define MOUSE_CURSOR_HEIGHT 28

struct mouse_cursor {
    struct graphics_context *graphics;

    const uint8_t *image;

    uint64_t x;
    uint64_t y;

    uint32_t saved_background[
        MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT
    ];

    bool visible;
};

bool mouse_cursor_init(
    struct mouse_cursor *cursor,
    struct graphics_context *graphics,
    const uint8_t *image,
    uint64_t image_size
);

void mouse_cursor_show(
    struct mouse_cursor *cursor
);

void mouse_cursor_hide(
    struct mouse_cursor *cursor
);

void mouse_cursor_move(
    struct mouse_cursor *cursor,
    int16_t delta_x,
    int16_t delta_y
);

#endif