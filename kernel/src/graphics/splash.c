#include "graphics/splash.h"

bool splash_show(
    struct graphics_context *graphics,
    const uint8_t *logo,
    size_t logo_size
)
{
    if (logo == NULL || logo_size != KRISHNA_LOGO_SIZE) {
        return false;
    }

    uint32_t background =
        graphics_rgb(graphics, 51, 170, 238);

    graphics_clear(graphics, background);

    uint64_t logo_x = 0;
    uint64_t logo_y = 20;

    if (graphics->framebuffer->width > KRISHNA_LOGO_WIDTH) {
        logo_x =
            (graphics->framebuffer->width - KRISHNA_LOGO_WIDTH) / 2;
    }

    graphics_blit_rgba(
        graphics,
        logo_x,
        logo_y,
        logo,
        KRISHNA_LOGO_WIDTH,
        KRISHNA_LOGO_HEIGHT
    );

    splash_set_progress(graphics, 0);
    return true;
}

void splash_set_progress(
    struct graphics_context *graphics,
    uint8_t percentage
)
{
    if (percentage > 100) {
        percentage = 100;
    }

    uint64_t bar_width = graphics->framebuffer->width / 2;

    if (bar_width > 600) {
        bar_width = 600;
    }

    uint64_t bar_x =
        (graphics->framebuffer->width - bar_width) / 2;

    uint64_t bar_y =
        graphics->framebuffer->height - 70;

    uint32_t border =
        graphics_rgb(graphics, 8, 35, 55);

    uint32_t empty =
        graphics_rgb(graphics, 34, 91, 120);

    uint32_t filled =
        graphics_rgb(graphics, 250, 207, 71);

    graphics_rectangle(
        graphics,
        bar_x,
        bar_y,
        bar_width,
        18,
        border
    );

    graphics_rectangle(
        graphics,
        bar_x + 3,
        bar_y + 3,
        bar_width - 6,
        12,
        empty
    );

    uint64_t completed_width =
        ((bar_width - 6) * percentage) / 100;

    graphics_rectangle(
        graphics,
        bar_x + 3,
        bar_y + 3,
        completed_width,
        12,
        filled
    );
}