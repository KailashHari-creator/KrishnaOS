#include "graphics/mouse_cursor.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>


bool mouse_cursor_init(
    struct mouse_cursor *cursor,
    struct graphics_context *graphics,
    const uint8_t *image,
    uint64_t image_size
) {
    const uint64_t expected_size =
        MOUSE_CURSOR_WIDTH *
        MOUSE_CURSOR_HEIGHT *
        4;

    if (cursor == NULL ||
        graphics == NULL ||
        image == NULL ||
        image_size != expected_size) {
        return false;
    }

    cursor->graphics = graphics;
    cursor->image = image;
    cursor->visible = false;

    /*
     * Begin in the centre of the display.
     */
    if (graphics->framebuffer->width >
        MOUSE_CURSOR_WIDTH) {
        cursor->x =
            (graphics->framebuffer->width -
             MOUSE_CURSOR_WIDTH) / 2;
    } else {
        cursor->x = 0;
    }

    if (graphics->framebuffer->height >
        MOUSE_CURSOR_HEIGHT) {
        cursor->y =
            (graphics->framebuffer->height -
             MOUSE_CURSOR_HEIGHT) / 2;
    } else {
        cursor->y = 0;
    }

    return true;
}


void mouse_cursor_show(
    struct mouse_cursor *cursor
) {
    if (cursor->visible) {
        return;
    }

    /*
     * Save every pixel currently beneath the pointer.
     */
    for (uint64_t y = 0;
         y < MOUSE_CURSOR_HEIGHT;
         y++) {
        for (uint64_t x = 0;
             x < MOUSE_CURSOR_WIDTH;
             x++) {

            uint64_t index =
                y * MOUSE_CURSOR_WIDTH + x;

            cursor->saved_background[index] =
                graphics_get_pixel(
                    cursor->graphics,
                    cursor->x + x,
                    cursor->y + y
                );
        }
    }

    graphics_blit_rgba(
        cursor->graphics,
        cursor->x,
        cursor->y,
        cursor->image,
        MOUSE_CURSOR_WIDTH,
        MOUSE_CURSOR_HEIGHT
    );

    cursor->visible = true;
}


void mouse_cursor_hide(
    struct mouse_cursor *cursor
) {
    if (!cursor->visible) {
        return;
    }

    /*
     * Restore the pixels that existed before the cursor
     * was drawn.
     */
    for (uint64_t y = 0;
         y < MOUSE_CURSOR_HEIGHT;
         y++) {
        for (uint64_t x = 0;
             x < MOUSE_CURSOR_WIDTH;
             x++) {

            uint64_t index =
                y * MOUSE_CURSOR_WIDTH + x;

            graphics_set_pixel(
                cursor->graphics,
                cursor->x + x,
                cursor->y + y,
                cursor->saved_background[index]
            );
        }
    }

    cursor->visible = false;
}


void mouse_cursor_move(
    struct mouse_cursor *cursor,
    int16_t delta_x,
    int16_t delta_y
) {
    if (delta_x == 0 && delta_y == 0) {
        return;
    }

    mouse_cursor_hide(cursor);

    int64_t new_x =
        (int64_t)cursor->x + delta_x;

    int64_t new_y =
        (int64_t)cursor->y + delta_y;

    int64_t maximum_x =
        (int64_t)cursor->graphics->framebuffer->width -
        MOUSE_CURSOR_WIDTH;

    int64_t maximum_y =
        (int64_t)cursor->graphics->framebuffer->height -
        MOUSE_CURSOR_HEIGHT;

    if (maximum_x < 0) {
        maximum_x = 0;
    }

    if (maximum_y < 0) {
        maximum_y = 0;
    }

    if (new_x < 0) {
        new_x = 0;
    }

    if (new_y < 0) {
        new_y = 0;
    }

    if (new_x > maximum_x) {
        new_x = maximum_x;
    }

    if (new_y > maximum_y) {
        new_y = maximum_y;
    }

    cursor->x = (uint64_t)new_x;
    cursor->y = (uint64_t)new_y;

    mouse_cursor_show(cursor);
}