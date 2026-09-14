#include "ui/desktop.h"

#include <stdbool.h>
#include <stdint.h>


static void desktop_glass_panel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint64_t radius
) {
    /*
     * Cyan outer rim.
     */
    graphics_rounded_rectangle(
        graphics,
        x,
        y,
        width,
        height,
        radius,
        72,
        220,
        245,
        180
    );

    /*
     * Translucent midnight interior.
     */
    if (width > 2 && height > 2) {
        graphics_rounded_rectangle(
            graphics,
            x + 1,
            y + 1,
            width - 2,
            height - 2,
            radius > 0 ? radius - 1 : 0,
            5,
            30,
            58,
            205
        );
    }
}


bool desktop_init(
    struct desktop *desktop,
    struct graphics_context *graphics,
    const uint8_t *wallpaper,
    uint64_t wallpaper_size
) {
    if (desktop == NULL ||
        graphics == NULL ||
        wallpaper == NULL) {
        return false;
    }

    const uint64_t expected_size =
        DESKTOP_WIDTH *
        DESKTOP_HEIGHT *
        4;

    if (wallpaper_size != expected_size) {
        return false;
    }

    /*
     * This first wallpaper is built specifically for our
     * current QEMU framebuffer resolution.
     */
    if (graphics->framebuffer->width != DESKTOP_WIDTH ||
        graphics->framebuffer->height != DESKTOP_HEIGHT) {
        return false;
    }

    desktop->graphics = graphics;
    desktop->wallpaper = wallpaper;

    desktop->terminal_width = 64;
    desktop->terminal_height = 64;

    desktop->terminal_x =
        (DESKTOP_WIDTH -
        desktop->terminal_width) / 2;

    desktop->terminal_y =
        DESKTOP_HEIGHT - 92 - 26 + 14;

    return true;
}


void desktop_render(
    struct desktop *desktop
) {
    struct graphics_context *graphics =
        desktop->graphics;

    /*
     * Paint the full desktop wallpaper.
     */
    graphics_blit_rgba(
        graphics,
        0,
        0,
        desktop->wallpaper,
        DESKTOP_WIDTH,
        DESKTOP_HEIGHT
    );

    /*
     * Top glass status bar.
     */
    desktop_glass_panel(
        graphics,
        0,
        0,
        DESKTOP_WIDTH,
        52,
        18
    );

    /*
     * Bottom centred application dock.
     */
    const uint64_t dock_width = 480;
    const uint64_t dock_height = 92;

    const uint64_t dock_x =
        (DESKTOP_WIDTH - dock_width) / 2;

    const uint64_t dock_y =
        DESKTOP_HEIGHT - dock_height - 26;

    desktop_glass_panel(
        graphics,
        dock_x,
        dock_y,
        dock_width,
        dock_height,
        24
    );

    desktop_draw_terminal_icon(desktop);
}

static void desktop_draw_terminal_icon(
    struct desktop *desktop
) {
    struct graphics_context *graphics =
        desktop->graphics;

    uint64_t x = desktop->terminal_x;
    uint64_t y = desktop->terminal_y;
    uint64_t width = desktop->terminal_width;
    uint64_t height = desktop->terminal_height;

    /*
     * Golden selected border.
     */
    graphics_rounded_rectangle(
        graphics,
        x - 2,
        y - 2,
        width + 4,
        height + 4,
        14,
        250,
        199,
        55,
        255
    );

    /*
     * Dark translucent icon body.
     */
    graphics_rounded_rectangle(
        graphics,
        x,
        y,
        width,
        height,
        12,
        5,
        24,
        45,
        245
    );

    /*
     * Draw the > symbol using small rectangles.
     */
    uint32_t symbol_colour =
        graphics_rgb(
            graphics,
            220,
            250,
            255
        );

    for (uint64_t i = 0; i <= 10; i++) {
        /*
         * Upper diagonal.
         */
        graphics_rectangle(
            graphics,
            x + 17 + i,
            y + 16 + i,
            3,
            3,
            symbol_colour
        );

        /*
         * Lower diagonal.
         */
        graphics_rectangle(
            graphics,
            x + 27 - i,
            y + 26 + i,
            3,
            3,
            symbol_colour
        );
    }

    /*
     * Terminal underscore.
     */
    graphics_rectangle(
        graphics,
        x + 35,
        y + 36,
        14,
        3,
        symbol_colour
    );

    /*
     * Golden selection indicator.
     */
    graphics_rounded_rectangle(
        graphics,
        x + 12,
        y + height + 5,
        40,
        4,
        2,
        250,
        199,
        55,
        255
    );
}
bool desktop_terminal_contains(
    const struct desktop *desktop,
    uint64_t x,
    uint64_t y
) {
    return
        x >= desktop->terminal_x &&
        y >= desktop->terminal_y &&
        x < desktop->terminal_x +
            desktop->terminal_width &&
        y < desktop->terminal_y +
            desktop->terminal_height;
}