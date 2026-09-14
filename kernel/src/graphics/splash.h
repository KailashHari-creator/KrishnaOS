#ifndef SPLASH_H
#define SPLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graphics/graphics.h"

#define KRISHNA_LOGO_WIDTH  627
#define KRISHNA_LOGO_HEIGHT 527
#define KRISHNA_LOGO_SIZE   \
    (KRISHNA_LOGO_WIDTH * KRISHNA_LOGO_HEIGHT * 4)

bool splash_show(
    struct graphics_context *graphics,
    const uint8_t *logo,
    size_t logo_size
);

void splash_set_progress(
    struct graphics_context *graphics,
    uint8_t percentage
);

#endif