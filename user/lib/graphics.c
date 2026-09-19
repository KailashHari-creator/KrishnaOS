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

static uint8_t rounded_rectangle_coverage(
    uint64_t pixel_x,
    uint64_t pixel_y,
    uint64_t rectangle_x,
    uint64_t rectangle_y,
    uint64_t width,
    uint64_t height,
    uint64_t radius
)
{
    if (width == 0 || height == 0) {
        return 0;
    }

    uint64_t maximum_radius =
        width < height
            ? width / 2
            : height / 2;

    if (radius > maximum_radius) {
        radius = maximum_radius;
    }

    if (radius == 0) {
        return 4;
    }

    /*
     * Coordinates use quarter-pixel units. Each real pixel is tested
     * at four subpixel positions:
     *
     *     (0.25, 0.25)  (0.75, 0.25)
     *     (0.25, 0.75)  (0.75, 0.75)
     */
    uint64_t left =
        rectangle_x * 4;

    uint64_t top =
        rectangle_y * 4;

    uint64_t right =
        (rectangle_x + width) * 4;

    uint64_t bottom =
        (rectangle_y + height) * 4;

    uint64_t radius_4 =
        radius * 4;

    uint64_t inner_left =
        left + radius_4;

    uint64_t inner_right =
        right - radius_4;

    uint64_t inner_top =
        top + radius_4;

    uint64_t inner_bottom =
        bottom - radius_4;

    static const uint64_t sample_offsets[2] = {
        UINT64_C(1),
        UINT64_C(3)
    };

    uint8_t coverage = 0;

    for (uint8_t sample_y = 0;
         sample_y < 2;
         sample_y++) {
        for (uint8_t sample_x = 0;
             sample_x < 2;
             sample_x++) {
            uint64_t x =
                pixel_x * 4 +
                sample_offsets[sample_x];

            uint64_t y =
                pixel_y * 4 +
                sample_offsets[sample_y];

            if (x < left ||
                x >= right ||
                y < top ||
                y >= bottom) {
                continue;
            }

            /*
             * The horizontal and vertical middle sections are always
             * inside the rounded rectangle.
             */
            if ((x >= inner_left &&
                 x < inner_right) ||
                (y >= inner_top &&
                 y < inner_bottom)) {
                coverage++;
                continue;
            }

            uint64_t center_x =
                x < inner_left
                    ? inner_left
                    : inner_right;

            uint64_t center_y =
                y < inner_top
                    ? inner_top
                    : inner_bottom;

            int64_t delta_x =
                (int64_t)x -
                (int64_t)center_x;

            int64_t delta_y =
                (int64_t)y -
                (int64_t)center_y;

            int64_t radius_squared =
                (int64_t)radius_4 *
                (int64_t)radius_4;

            if (delta_x * delta_x +
                    delta_y * delta_y <=
                radius_squared) {
                coverage++;
            }
        }
    }

    return coverage;
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
    if (graphics == NULL ||
        width == 0 ||
        height == 0 ||
        alpha == 0) {
        return;
    }

    /*
     * Clamp the drawing bounds before iterating. This prevents the
     * loops from attempting to draw past the framebuffer.
     */
    if (x >= graphics->info.width ||
        y >= graphics->info.height) {
        return;
    }

    uint64_t clipped_width =
        width;

    uint64_t clipped_height =
        height;

    if (clipped_width >
        graphics->info.width - x) {
        clipped_width =
            graphics->info.width - x;
    }

    if (clipped_height >
        graphics->info.height - y) {
        clipped_height =
            graphics->info.height - y;
    }

    for (uint64_t row = 0;
         row < clipped_height;
         row++) {
        for (uint64_t column = 0;
             column < clipped_width;
             column++) {
            uint8_t coverage =
                rounded_rectangle_coverage(
                    x + column,
                    y + row,
                    x,
                    y,
                    width,
                    height,
                    radius
                );

            if (coverage == 0) {
                continue;
            }

            /*
             * Convert four-sample coverage into the final alpha:
             *
             * coverage 1 = 25%
             * coverage 2 = 50%
             * coverage 3 = 75%
             * coverage 4 = 100%
             */
            uint8_t final_alpha =
                (uint8_t)(
                    (
                        (uint32_t)alpha *
                        coverage +
                        UINT32_C(2)
                    ) /
                    UINT32_C(4)
                );

            graphics_blend_pixel(
                graphics,
                x + column,
                y + row,
                red,
                green,
                blue,
                final_alpha
            );
        }
    }
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

bool graphics_present(
    struct graphics_context *destination,
    const struct graphics_context *source
)
{
    if (destination == NULL ||
        source == NULL ||
        destination->info.width !=
            source->info.width ||
        destination->info.height !=
            source->info.height ||
        destination->info.bits_per_pixel != 32 ||
        source->info.bits_per_pixel != 32) {
        return false;
    }

    for (uint64_t y = 0;
         y < source->info.height;
         y++) {
        volatile uint32_t *destination_row =
            (volatile uint32_t *)(void *)(
                destination->address +
                y * destination->info.pitch
            );

        const volatile uint32_t *source_row =
            (const volatile uint32_t *)
                (const void *)(
                    source->address +
                    y * source->info.pitch
                );

        for (uint64_t x = 0;
             x < source->info.width;
             x++) {
            destination_row[x] =
                source_row[x];
        }
    }

    return true;
}
