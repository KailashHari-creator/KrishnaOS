#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <limine.h>
#include <stdbool.h>

struct graphics_context {
    struct limine_framebuffer *framebuffer;
    volatile uint32_t *pixels;
    uint64_t stride;
};

void graphics_init(
    struct graphics_context *graphics,
    struct limine_framebuffer *framebuffer
);

uint32_t graphics_rgb(
    struct graphics_context *graphics,
    uint8_t red,
    uint8_t green,
    uint8_t blue
);

void graphics_clear(
    struct graphics_context *graphics,
    uint32_t colour
);

void graphics_rectangle(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t colour
);

void graphics_blit_rgba(
    struct graphics_context *graphics,
    uint64_t destination_x,
    uint64_t destination_y,
    const uint8_t *source,
    uint64_t source_width,
    uint64_t source_height
);

void graphics_blend_pixel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
);

uint32_t graphics_get_pixel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y
);

void graphics_set_pixel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint32_t colour
);

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
);
#endif