#include "graphics/graphics.h"

void graphics_init(
    struct graphics_context *graphics,
    struct limine_framebuffer *framebuffer
)
{
    graphics->framebuffer = framebuffer;
    graphics->pixels =
        (volatile uint32_t *)framebuffer->address;
    graphics->stride =
        framebuffer->pitch / sizeof(uint32_t);
}

uint32_t graphics_rgb(
    struct graphics_context *graphics,
    uint8_t red,
    uint8_t green,
    uint8_t blue
)
{
    struct limine_framebuffer *fb = graphics->framebuffer;

    return ((uint32_t)red << fb->red_mask_shift)
         | ((uint32_t)green << fb->green_mask_shift)
         | ((uint32_t)blue << fb->blue_mask_shift);
}

void graphics_rectangle(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t colour
)
{
    struct limine_framebuffer *fb = graphics->framebuffer;

    if (x >= fb->width || y >= fb->height) {
        return;
    }

    if (width > fb->width - x) {
        width = fb->width - x;
    }

    if (height > fb->height - y) {
        height = fb->height - y;
    }

    for (uint64_t row = 0; row < height; row++) {
        for (uint64_t column = 0; column < width; column++) {
            graphics->pixels[
                (y + row) * graphics->stride + x + column
            ] = colour;
        }
    }
}

void graphics_clear(
    struct graphics_context *graphics,
    uint32_t colour
)
{
    graphics_rectangle(
        graphics,
        0,
        0,
        graphics->framebuffer->width,
        graphics->framebuffer->height,
        colour
    );
}

void graphics_blit_rgba(
    struct graphics_context *graphics,
    uint64_t destination_x,
    uint64_t destination_y,
    const uint8_t *source,
    uint64_t source_width,
    uint64_t source_height
)
{
    for (uint64_t y = 0; y < source_height; y++) {
        uint64_t screen_y = destination_y + y;

        if (screen_y >= graphics->framebuffer->height) {
            break;
        }

        for (uint64_t x = 0; x < source_width; x++) {
            uint64_t screen_x = destination_x + x;

            if (screen_x >= graphics->framebuffer->width) {
                break;
            }

            uint64_t source_index =
                (y * source_width + x) * 4;

            uint8_t red   = source[source_index];
            uint8_t green = source[source_index + 1];
            uint8_t blue  = source[source_index + 2];
            uint8_t alpha = source[source_index + 3];

            graphics_blend_pixel(
                graphics,
                screen_x,
                screen_y,
                red,
                green,
                blue,
                alpha
            ); 
        }
    }
}

void graphics_blend_pixel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
)
{
    struct limine_framebuffer *fb = graphics->framebuffer;

    if (x >= fb->width || y >= fb->height || alpha == 0) {
        return;
    }

    uint64_t index = y * graphics->stride + x;

    if (alpha == 255) {
        graphics->pixels[index] =
            graphics_rgb(graphics, red, green, blue);
        return;
    }

    uint32_t old_pixel = graphics->pixels[index];

    uint8_t old_red =
        (uint8_t)(old_pixel >> fb->red_mask_shift);

    uint8_t old_green =
        (uint8_t)(old_pixel >> fb->green_mask_shift);

    uint8_t old_blue =
        (uint8_t)(old_pixel >> fb->blue_mask_shift);

    uint8_t new_red = (uint8_t)(
        (red * alpha + old_red * (255 - alpha)) / 255
    );

    uint8_t new_green = (uint8_t)(
        (green * alpha + old_green * (255 - alpha)) / 255
    );

    uint8_t new_blue = (uint8_t)(
        (blue * alpha + old_blue * (255 - alpha)) / 255
    );

    graphics->pixels[index] = graphics_rgb(
        graphics,
        new_red,
        new_green,
        new_blue
    );
}

uint32_t graphics_get_pixel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y
) {
    if (x >= graphics->framebuffer->width ||
        y >= graphics->framebuffer->height) {
        return 0;
    }

    return graphics->pixels[
        y * graphics->stride + x
    ];
}


void graphics_set_pixel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint32_t colour
) {
    if (x >= graphics->framebuffer->width ||
        y >= graphics->framebuffer->height) {
        return;
    }

    graphics->pixels[
        y * graphics->stride + x
    ] = colour;
}

void graphics_rounded_rectangle(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint64_t radius,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
) {
    if (width == 0 || height == 0) {
        return;
    }

    if (radius > width / 2) {
        radius = width / 2;
    }

    if (radius > height / 2) {
        radius = height / 2;
    }

    for (uint64_t py = 0; py < height; py++) {
        for (uint64_t px = 0; px < width; px++) {
            int64_t distance_x = 0;
            int64_t distance_y = 0;

            /*
             * Measure distance from the nearest rounded-corner centre.
             */
            if (px < radius) {
                distance_x =
                    (int64_t)radius -
                    (int64_t)px;
            } else if (px >= width - radius) {
                distance_x =
                    (int64_t)px -
                    (int64_t)(width - radius - 1);
            }

            if (py < radius) {
                distance_y =
                    (int64_t)radius -
                    (int64_t)py;
            } else if (py >= height - radius) {
                distance_y =
                    (int64_t)py -
                    (int64_t)(height - radius - 1);
            }

            bool inside =
                distance_x * distance_x +
                distance_y * distance_y <=
                (int64_t)(radius * radius);

            if (!inside) {
                continue;
            }

            graphics_blend_pixel(
                graphics,
                x + px,
                y + py,
                red,
                green,
                blue,
                alpha
            );
        }
    }
}