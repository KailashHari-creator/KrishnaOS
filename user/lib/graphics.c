#include <krishna/graphics.h>

#include <stddef.h>
#include <stdint.h>

static volatile uint32_t *pixel_address(
    const struct graphics_context *graphics,
    uint64_t x,
    uint64_t y
)
{
    return
        (volatile uint32_t *)(void *)(
            graphics->address +
            y * graphics->info.pitch +
            x * sizeof(uint32_t)
        );
}

bool graphics_init(
    struct graphics_context *graphics,
    void *address,
    const struct krishna_framebuffer_info *info
)
{
    if (graphics == NULL ||
        address == NULL ||
        info == NULL ||
        info->width == 0 ||
        info->height == 0 ||
        info->bits_per_pixel != 32 ||
        info->pitch <
            info->width * sizeof(uint32_t)) {
        return false;
    }

    graphics->info = *info;
    graphics->address =
        (volatile uint8_t *)address;

    return true;
}

uint32_t graphics_rgb(
    const struct graphics_context *graphics,
    uint8_t red,
    uint8_t green,
    uint8_t blue
)
{
    return
        ((uint32_t)red <<
            graphics->info.red_mask_shift) |
        ((uint32_t)green <<
            graphics->info.green_mask_shift) |
        ((uint32_t)blue <<
            graphics->info.blue_mask_shift);
}

uint32_t graphics_get_pixel(
    const struct graphics_context *graphics,
    uint64_t x,
    uint64_t y
)
{
    if (x >= graphics->info.width ||
        y >= graphics->info.height) {
        return 0;
    }

    return *pixel_address(
        graphics,
        x,
        y
    );
}

void graphics_set_pixel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint32_t colour
)
{
    if (x >= graphics->info.width ||
        y >= graphics->info.height) {
        return;
    }

    *pixel_address(
        graphics,
        x,
        y
    ) = colour;
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
    if (x >= graphics->info.width ||
        y >= graphics->info.height) {
        return;
    }

    if (width >
        graphics->info.width - x) {
        width =
            graphics->info.width - x;
    }

    if (height >
        graphics->info.height - y) {
        height =
            graphics->info.height - y;
    }

    for (uint64_t row = 0;
         row < height;
         row++) {
        for (uint64_t column = 0;
             column < width;
             column++) {
            graphics_set_pixel(
                graphics,
                x + column,
                y + row,
                colour
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
    if (x >= graphics->info.width ||
        y >= graphics->info.height ||
        alpha == 0) {
        return;
    }

    if (alpha == UINT8_MAX) {
        graphics_set_pixel(
            graphics,
            x,
            y,
            graphics_rgb(
                graphics,
                red,
                green,
                blue
            )
        );

        return;
    }

    uint32_t old_pixel =
        graphics_get_pixel(
            graphics,
            x,
            y
        );

    uint8_t old_red =
        (uint8_t)(
            old_pixel >>
            graphics->info.red_mask_shift
        );

    uint8_t old_green =
        (uint8_t)(
            old_pixel >>
            graphics->info.green_mask_shift
        );

    uint8_t old_blue =
        (uint8_t)(
            old_pixel >>
            graphics->info.blue_mask_shift
        );

    uint8_t new_red =
        (uint8_t)(
            (
                (uint32_t)red * alpha +
                (uint32_t)old_red *
                    (UINT8_MAX - alpha)
            ) / UINT8_MAX
        );

    uint8_t new_green =
        (uint8_t)(
            (
                (uint32_t)green * alpha +
                (uint32_t)old_green *
                    (UINT8_MAX - alpha)
            ) / UINT8_MAX
        );

    uint8_t new_blue =
        (uint8_t)(
            (
                (uint32_t)blue * alpha +
                (uint32_t)old_blue *
                    (UINT8_MAX - alpha)
            ) / UINT8_MAX
        );

    graphics_set_pixel(
        graphics,
        x,
        y,
        graphics_rgb(
            graphics,
            new_red,
            new_green,
            new_blue
        )
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
    if (source == NULL) {
        return;
    }

    for (uint64_t y = 0;
         y < source_height;
         y++) {
        uint64_t screen_y =
            destination_y + y;

        if (screen_y >=
            graphics->info.height) {
            break;
        }

        for (uint64_t x = 0;
             x < source_width;
             x++) {
            uint64_t screen_x =
                destination_x + x;

            if (screen_x >=
                graphics->info.width) {
                break;
            }

            uint64_t source_index =
                (y * source_width + x) * 4;

            graphics_blend_pixel(
                graphics,
                screen_x,
                screen_y,
                source[source_index],
                source[source_index + 1],
                source[source_index + 2],
                source[source_index + 3]
            );
        }
    }
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
)
{
    if (width == 0 || height == 0) {
        return;
    }

    if (radius > width / 2) {
        radius = width / 2;
    }

    if (radius > height / 2) {
        radius = height / 2;
    }

    for (uint64_t py = 0;
         py < height;
         py++) {
        for (uint64_t px = 0;
             px < width;
             px++) {
            int64_t distance_x = 0;
            int64_t distance_y = 0;

            if (px < radius) {
                distance_x =
                    (int64_t)radius -
                    (int64_t)px;
            } else if (
                px >= width - radius
            ) {
                distance_x =
                    (int64_t)px -
                    (int64_t)(
                        width - radius - 1
                    );
            }

            if (py < radius) {
                distance_y =
                    (int64_t)radius -
                    (int64_t)py;
            } else if (
                py >= height - radius
            ) {
                distance_y =
                    (int64_t)py -
                    (int64_t)(
                        height - radius - 1
                    );
            }

            bool inside =
                distance_x * distance_x +
                distance_y * distance_y <=
                (int64_t)(
                    radius * radius
                );

            if (inside) {
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
}