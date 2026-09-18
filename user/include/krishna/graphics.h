#ifndef KRISHNA_USER_GRAPHICS_H
#define KRISHNA_USER_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#include <krishna/abi.h>

struct graphics_context {
    struct krishna_framebuffer_info info;
    volatile uint8_t *address;
};

bool graphics_init(
    struct graphics_context *graphics,
    void *address,
    const struct krishna_framebuffer_info *info
);

uint32_t graphics_rgb(
    const struct graphics_context *graphics,
    uint8_t red,
    uint8_t green,
    uint8_t blue
);

uint32_t graphics_get_pixel(
    const struct graphics_context *graphics,
    uint64_t x,
    uint64_t y
);

void graphics_set_pixel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
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

void graphics_blend_pixel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
);

void graphics_blit_rgba(
    struct graphics_context *graphics,
    uint64_t destination_x,
    uint64_t destination_y,
    const uint8_t *source,
    uint64_t source_width,
    uint64_t source_height
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