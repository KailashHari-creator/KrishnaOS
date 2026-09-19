#include "frontend.h"

#define DESKTOP_WIDTH  UINT64_C(1280)
#define DESKTOP_HEIGHT UINT64_C(800)

#define TERMINAL_ICON_WIDTH  UINT64_C(72)
#define TERMINAL_ICON_HEIGHT UINT64_C(72)

#define SHELF_X      UINT64_C(370)
#define SHELF_Y      UINT64_C(690)
#define SHELF_WIDTH  UINT64_C(540)
#define SHELF_HEIGHT UINT64_C(70)

#define TERMINAL_WINDOW_X      UINT64_C(180)
#define TERMINAL_WINDOW_Y      UINT64_C(105)
#define TERMINAL_WINDOW_WIDTH  UINT64_C(920)
#define TERMINAL_WINDOW_HEIGHT UINT64_C(500)
#define TERMINAL_ART_WIDTH  UINT64_C(520)
#define TERMINAL_ART_HEIGHT UINT64_C(250)

extern const uint8_t krishna_wallpaper_start[];
extern const uint8_t krishna_wallpaper_end[];

extern const uint8_t krishna_terminal_icon_start[];
extern const uint8_t krishna_terminal_icon_end[];

extern const uint8_t krishna_font_start[];
extern const uint8_t krishna_font_end[];

extern const uint8_t krishna_terminal_art_start[];
extern const uint8_t krishna_terminal_art_end[];

struct __attribute__((packed)) kfont_header {
    char magic[8];

    uint16_t glyph_width;
    uint16_t glyph_height;
    uint16_t first_character;
    uint16_t glyph_count;
};

static bool font_magic_valid(
    const char magic[8]
)
{
    static const char expected[8] = {
        'K', 'F', 'O', 'N', 'T', '0', '0', '1'
    };

    for (uint8_t index = 0;
         index < 8;
         index++) {
        if (magic[index] != expected[index]) {
            return false;
        }
    }

    return true;
}

static bool font_initialize(
    struct desktop_font *font
)
{
    size_t file_size =
        (size_t)(
            krishna_font_end -
            krishna_font_start
        );

    if (file_size < sizeof(struct kfont_header)) {
        return false;
    }

    const struct kfont_header *header =
        (const struct kfont_header *)
            krishna_font_start;

    if (!font_magic_valid(header->magic)) {
        return false;
    }

    size_t required_size =
        sizeof(struct kfont_header) +
        (size_t)header->glyph_width *
        (size_t)header->glyph_height *
        (size_t)header->glyph_count;

    if (file_size != required_size) {
        return false;
    }

    if (header->glyph_width == 0 ||
        header->glyph_height == 0 ||
        header->glyph_count == 0) {
        return false;
    }

    font->glyphs =
        krishna_font_start +
        sizeof(struct kfont_header);

    font->glyph_width =
        header->glyph_width;

    font->glyph_height =
        header->glyph_height;

    font->first_character =
        header->first_character;

    font->glyph_count =
        header->glyph_count;

    return true;
}

static void draw_character(
    struct desktop_frontend *desktop,
    uint64_t x,
    uint64_t y,
    char character,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
)
{
    struct desktop_font *font =
        &desktop->font;

    uint8_t code =
        (uint8_t)character;

    uint32_t last_character =
        (uint32_t)font->first_character +
        font->glyph_count;

    if ((uint32_t)code < font->first_character ||
        (uint32_t)code >= last_character) {
        code = (uint8_t)'?';
    }

    uint64_t glyph_index =
        (uint64_t)code -
        font->first_character;

    const uint8_t *glyph =
        font->glyphs +
        glyph_index *
        font->glyph_width *
        font->glyph_height;

    for (uint16_t glyph_y = 0;
         glyph_y < font->glyph_height;
         glyph_y++) {
        for (uint16_t glyph_x = 0;
             glyph_x < font->glyph_width;
             glyph_x++) {
            uint8_t glyph_alpha =
                glyph[
                    (uint64_t)glyph_y *
                    font->glyph_width +
                    glyph_x
                ];

            uint8_t final_alpha =
                (uint8_t)(
                    (uint16_t)glyph_alpha *
                    alpha /
                    UINT8_MAX
                );

            graphics_blend_pixel(
                desktop->graphics,
                x + glyph_x,
                y + glyph_y,
                red,
                green,
                blue,
                final_alpha
            );
        }
    }
}

static void draw_text(
    struct desktop_frontend *desktop,
    uint64_t x,
    uint64_t y,
    const char *text,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
)
{
    uint64_t cursor_x = x;

    while (*text != '\0') {
        draw_character(
            desktop,
            cursor_x,
            y,
            *text,
            red,
            green,
            blue,
            alpha
        );

        cursor_x +=
            desktop->font.glyph_width;

        text++;
    }
}

static uint64_t text_width(
    const struct desktop_frontend *desktop,
    const char *text
)
{
    uint64_t character_count = 0;

    while (*text != '\0') {
        character_count++;
        text++;
    }

    if (character_count == 0) {
        return 0;
    }

    return
        character_count * desktop->font.glyph_width;
}

static void draw_filled_circle(
    struct graphics_context *graphics,
    uint64_t center_x,
    uint64_t center_y,
    uint64_t radius,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
)
{
    int64_t signed_radius =
        (int64_t)radius;

    for (int64_t y = -signed_radius;
         y <= signed_radius;
         y++) {
        for (int64_t x = -signed_radius;
             x <= signed_radius;
             x++) {
            if (x * x + y * y >
                signed_radius * signed_radius) {
                continue;
            }

            int64_t destination_x =
                (int64_t)center_x + x;

            int64_t destination_y =
                (int64_t)center_y + y;

            if (destination_x < 0 ||
                destination_y < 0) {
                continue;
            }

            graphics_blend_pixel(
                graphics,
                (uint64_t)destination_x,
                (uint64_t)destination_y,
                red,
                green,
                blue,
                alpha
            );
        }
    }
}

static void draw_glass_panel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint64_t radius
)
{
    /*
     * Detached shadow.
     */
    graphics_rounded_rectangle(
        graphics,
        x + 7,
        y + 9,
        width,
        height,
        radius,
        0,
        3,
        14,
        88
    );

    /*
     * Cyan edge.
     */
    graphics_rounded_rectangle(
        graphics,
        x,
        y,
        width,
        height,
        radius,
        50,
        210,
        245,
        185
    );

    /*
     * Dark translucent interior.
     */
    graphics_rounded_rectangle(
        graphics,
        x + 1,
        y + 1,
        width - 2,
        height - 2,
        radius > 0 ? radius - 1 : 0,
        5,
        29,
        52,
        225
    );

    /*
     * Subtle top highlight.
     */
    graphics_rounded_rectangle(
        graphics,
        x + radius,
        y + 1,
        width - radius * 2,
        2,
        1,
        135,
        235,
        255,
        65
    );
}

static void draw_terminal_glass(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint64_t radius
)
{
    /*
     * Deep shadow beneath the window.
     */
    graphics_rounded_rectangle(
        graphics,
        x + 12,
        y + 16,
        width,
        height,
        radius,
        0,
        2,
        12,
        112
    );

    /*
     * Softer secondary shadow.
     */
    graphics_rounded_rectangle(
        graphics,
        x + 6,
        y + 9,
        width,
        height,
        radius,
        0,
        7,
        20,
        72
    );

    /*
     * Thin cyan window rim.
     */
    graphics_rounded_rectangle(
        graphics,
        x,
        y,
        width,
        height,
        radius,
        55,
        204,
        240,
        205
    );

    /*
     * Main terminal surface.

     * Alpha 246 gives roughly 96% opacity. The wallpaper is only
     * faintly visible through it, so terminal text remains readable.
     */
    graphics_rounded_rectangle(
        graphics,
        x + 1,
        y + 1,
        width - 2,
        height - 2,
        radius > 0 ? radius - 1 : 0,
        2,
        11,
        25,
        220
    );

    /*
     * Compact title-bar surface.
     */
    graphics_rounded_rectangle(
        graphics,
        x + 3,
        y + 3,
        width - 6,
        45,
        radius > 3 ? radius - 3 : 0,
        16,
        53,
        79,
        190
    );

    /*
     * Cover the rounded lower corners of the title-bar layer so only
     * the actual window corners remain rounded.
     */
    graphics_rounded_rectangle(
        graphics,
        x + 3,
        y + 24,
        width - 6,
        23,
        0,
        10,
        38,
        62,
        185
    );

    /*
     * Title separator.
     */
    graphics_rounded_rectangle(
        graphics,
        x + 2,
        y + 47,
        width - 4,
        1,
        0,
        73,
        201,
        235,
        110
    );

    /*
     * Restrained top specular line.
     */
    graphics_rounded_rectangle(
        graphics,
        x + radius,
        y + 1,
        width - radius * 2,
        1,
        0,
        176,
        245,
        255,
        72
    );
}

static void draw_top_panel(
    struct desktop_frontend *desktop
)
{
    draw_glass_panel(
        desktop->graphics,
        0,
        0,
        DESKTOP_WIDTH,
        50,
        16
    );

    draw_text(
        desktop,
        24,
        16,
        "KRISHNA OS",
        220,
        245,
        255,
        255
    );

    draw_text(
        desktop,
        1080,
        16,
        "RING 3 DESKTOP",
        115,
        215,
        245,
        210
    );
}

static void draw_shelf(
    struct desktop_frontend *desktop
)
{
    struct graphics_context *graphics =
        desktop->graphics;

    /*
     * Shelf shadow.
     */
    graphics_rounded_rectangle(
        graphics,
        SHELF_X - 10,
        SHELF_Y + 16,
        SHELF_WIDTH + 20,
        SHELF_HEIGHT,
        24,
        0,
        8,
        24,
        110
    );

    /*
     * Perspective top surface. The shelf dimensions deliberately
     * remain wide for future application icons.
     */
    const uint64_t surface_height = 48;

    for (uint64_t row = 0;
         row < surface_height;
         row++) {
        uint64_t inset =
            40 -
            row * 40 /
            (surface_height - 1);

        uint64_t row_x =
            SHELF_X + inset;

        uint64_t row_width =
            SHELF_WIDTH - inset * 2;

        uint8_t alpha =
            (uint8_t)(
                68 +
                row * 48 /
                surface_height
            );

        for (uint64_t column = 0;
             column < row_width;
             column++) {
            graphics_blend_pixel(
                graphics,
                row_x + column,
                SHELF_Y + row,
                22,
                105,
                165,
                alpha
            );
        }
    }

    /*
     * Rear illuminated edge.
     */
    graphics_rounded_rectangle(
        graphics,
        SHELF_X + 40,
        SHELF_Y,
        SHELF_WIDTH - 80,
        3,
        2,
        100,
        235,
        255,
        220
    );

    /*
     * Glass front lip.
     */
    graphics_rounded_rectangle(
        graphics,
        SHELF_X,
        SHELF_Y + 43,
        SHELF_WIDTH,
        27,
        12,
        55,
        205,
        250,
        190
    );

    graphics_rounded_rectangle(
        graphics,
        SHELF_X + 3,
        SHELF_Y + 46,
        SHELF_WIDTH - 6,
        20,
        9,
        4,
        35,
        75,
        220
    );

    graphics_rounded_rectangle(
        graphics,
        SHELF_X + 10,
        SHELF_Y + 45,
        SHELF_WIDTH - 20,
        2,
        1,
        170,
        245,
        255,
        220
    );
}

static void draw_icon_reflection(
    struct desktop_frontend *desktop,
    uint64_t x,
    uint64_t y
)
{
    const uint64_t reflection_height = 30;

    for (uint64_t destination_y = 0;
         destination_y < reflection_height;
         destination_y++) {
        uint64_t source_y =
            TERMINAL_ICON_HEIGHT - 1 -
            destination_y *
            TERMINAL_ICON_HEIGHT /
            reflection_height;

        uint32_t fade =
            (uint32_t)(
                reflection_height -
                destination_y
            );

        for (uint64_t source_x = 0;
             source_x < TERMINAL_ICON_WIDTH;
             source_x++) {
            uint64_t source_index =
                (
                    source_y *
                    TERMINAL_ICON_WIDTH +
                    source_x
                ) * 4;

            uint8_t source_alpha =
                desktop->terminal_icon[
                    source_index + 3
                ];

            uint8_t reflection_alpha =
                (uint8_t)(
                    (uint32_t)source_alpha *
                    fade /
                    reflection_height /
                    3
                );

            graphics_blend_pixel(
                desktop->graphics,
                x + source_x,
                y + destination_y,
                desktop->terminal_icon[
                    source_index
                ],
                desktop->terminal_icon[
                    source_index + 1
                ],
                desktop->terminal_icon[
                    source_index + 2
                ],
                reflection_alpha
            );
        }
    }
}

static uint64_t icon_render_y(
    const struct desktop_frontend *desktop
)
{
    /*
     * A four-pixel movement is enough to communicate hover without
     * making the icon jump aggressively.
     */
    return
        desktop->terminal_icon_y -
        (desktop->terminal_hovered ? 4 : 0);
}

static void draw_terminal_icon(
    struct desktop_frontend *desktop
)
{
    uint64_t x =
        desktop->terminal_icon_x;

    uint64_t y =
        icon_render_y(desktop);

    /*
     * Small shadow directly beneath the icon.
     */
    graphics_rounded_rectangle(
        desktop->graphics,
        x + 7,
        y + 10,
        TERMINAL_ICON_WIDTH - 14,
        TERMINAL_ICON_HEIGHT - 4,
        18,
        0,
        8,
        25,
        130
    );

    if (desktop->terminal_hovered) {
        /*
         * Soft cyan aura. This replaces the thick gold wireframe.
         */
        graphics_rounded_rectangle(
            desktop->graphics,
            x - 7,
            y - 7,
            TERMINAL_ICON_WIDTH + 14,
            TERMINAL_ICON_HEIGHT + 14,
            20,
            43,
            211,
            255,
            30
        );

        graphics_rounded_rectangle(
            desktop->graphics,
            x - 3,
            y - 3,
            TERMINAL_ICON_WIDTH + 6,
            TERMINAL_ICON_HEIGHT + 6,
            17,
            92,
            230,
            255,
            32
        );
    }

    graphics_blit_rgba(
        desktop->graphics,
        x,
        y,
        desktop->terminal_icon,
        TERMINAL_ICON_WIDTH,
        TERMINAL_ICON_HEIGHT
    );

    /*
     * Reflection remains anchored to the shelf. It does not jump when
     * the real icon rises during hover.
     */
    draw_icon_reflection(
        desktop,
        x,
        SHELF_Y + 17
    );

    if (desktop->terminal_hovered) {
        /*
         * Restrained golden focus indicator.
         */
        graphics_rounded_rectangle(
            desktop->graphics,
            x + 18,
            y + TERMINAL_ICON_HEIGHT + 7,
            TERMINAL_ICON_WIDTH - 36,
            3,
            1,
            255,
            199,
            49,
            235
        );

        draw_text(
            desktop,
            x + 1,
            y - 26,
            "Terminal",
            220,
            248,
            255,
            240
        );
    }
}

static void draw_terminal_window(
    struct desktop_frontend *desktop
)
{
    if (!desktop->terminal_open) {
        return;
    }

    static const char prompt[] =
        "kailash@krishna:~$ ";

    struct graphics_context *graphics =
        desktop->graphics;

    draw_terminal_glass(
        graphics,
        TERMINAL_WINDOW_X,
        TERMINAL_WINDOW_Y,
        TERMINAL_WINDOW_WIDTH,
        TERMINAL_WINDOW_HEIGHT,
        24
    );

    /*
     * Window controls.
     */
    draw_filled_circle(
        graphics,
        TERMINAL_WINDOW_X + 26,
        TERMINAL_WINDOW_Y + 24,
        7,
        255,
        93,
        100,
        255
    );

    draw_filled_circle(
        graphics,
        TERMINAL_WINDOW_X + 50,
        TERMINAL_WINDOW_Y + 24,
        7,
        250,
        199,
        55,
        255
    );

    draw_filled_circle(
        graphics,
        TERMINAL_WINDOW_X + 74,
        TERMINAL_WINDOW_Y + 24,
        7,
        80,
        225,
        145,
        255
    );

    draw_text(
        desktop,
        TERMINAL_WINDOW_X + 108,
        TERMINAL_WINDOW_Y + 15,
        "KRISHNA TERMINAL",
        220,
        248,
        255,
        255
    );

    /*
     * Anchor the prompt to the bottom of the terminal. Future command
     * output should grow upward from this position.
     */
    const uint64_t bottom_padding = 24;

    uint64_t prompt_x =
        TERMINAL_WINDOW_X + 30;

    uint64_t prompt_y =
        TERMINAL_WINDOW_Y +
        TERMINAL_WINDOW_HEIGHT -
        bottom_padding -
        desktop->font.glyph_height;

    uint64_t line_spacing =
        desktop->font.glyph_height + 14;

    uint64_t information_y =
        prompt_y - line_spacing;

    uint64_t welcome_y =
        information_y - line_spacing;

    /*
     * Place the artwork immediately above the startup messages.
     */
    uint64_t art_x =
        TERMINAL_WINDOW_X +
        (
            TERMINAL_WINDOW_WIDTH -
            TERMINAL_ART_WIDTH
        ) / 2;

    uint64_t art_y =
        welcome_y -
        TERMINAL_ART_HEIGHT -
        18;

    uint64_t minimum_art_y =
        TERMINAL_WINDOW_Y + 62;

    if (art_y < minimum_art_y) {
        art_y = minimum_art_y;
    }

    graphics_blit_rgba(
        graphics,
        art_x,
        art_y,
        krishna_terminal_art_start,
        TERMINAL_ART_WIDTH,
        TERMINAL_ART_HEIGHT
    );

    draw_text(
        desktop,
        prompt_x,
        welcome_y,
        "Welcome to KRISHNA OS.",
        215,
        240,
        248,
        255
    );

    draw_text(
        desktop,
        prompt_x,
        information_y,
        "The terminal frontend is running in Ring 3.",
        110,
        210,
        240,
        230
    );

    draw_text(
        desktop,
        prompt_x,
        prompt_y,
        prompt,
        65,
        235,
        245,
        255
    );

    uint64_t input_x =
        prompt_x +
        text_width(desktop, prompt);

    draw_text(
        desktop,
        input_x,
        prompt_y,
        desktop->terminal_input,
        225,
        242,
        248,
        255
    );

    /*
     * Position the cursor from the actual prompt width rather than
     * using a hard-coded coordinate.
     */
    uint64_t cursor_x =
        input_x +
        text_width(
            desktop,
            desktop->terminal_input
        ) +
        4;

    graphics_rounded_rectangle(
        graphics,
        cursor_x,
        prompt_y,
        9,
        desktop->font.glyph_height,
        2,
        250,
        199,
        55,
        255
    );

    /*
     * Scrollbar track.
     */
    graphics_rounded_rectangle(
        graphics,
        TERMINAL_WINDOW_X +
            TERMINAL_WINDOW_WIDTH - 18,
        TERMINAL_WINDOW_Y + 62,
        5,
        TERMINAL_WINDOW_HEIGHT - 82,
        2,
        105,
        170,
        205,
        55
    );

    /*
     * Scrollbar thumb starts at the bottom because this initial view
     * represents the newest terminal output.
     */
    const uint64_t scrollbar_thumb_height = 105;

    graphics_rounded_rectangle(
        graphics,
        TERMINAL_WINDOW_X +
            TERMINAL_WINDOW_WIDTH - 18,
        TERMINAL_WINDOW_Y +
            TERMINAL_WINDOW_HEIGHT -
            scrollbar_thumb_height -
            20,
        5,
        scrollbar_thumb_height,
        2,
        115,
        225,
        250,
        175
    );
}

static bool terminal_icon_contains(
    const struct desktop_frontend *desktop,
    uint64_t x,
    uint64_t y
)
{
    /*
     * The hitbox uses the icon's resting position. Hover movement must
     * never move the hitbox itself.
     */
    const uint64_t hitbox_padding = 14;

    uint64_t left =
        desktop->terminal_icon_x >= hitbox_padding
            ? desktop->terminal_icon_x - hitbox_padding
            : 0;

    uint64_t top =
        desktop->terminal_icon_y >= hitbox_padding
            ? desktop->terminal_icon_y - hitbox_padding
            : 0;

    uint64_t right =
        desktop->terminal_icon_x +
        TERMINAL_ICON_WIDTH +
        hitbox_padding;

    uint64_t bottom =
        desktop->terminal_icon_y +
        TERMINAL_ICON_HEIGHT +
        hitbox_padding;

    return
        x >= left &&
        x < right &&
        y >= top &&
        y < bottom;
}

static bool terminal_close_contains(
    uint64_t x,
    uint64_t y
)
{
    return
        x >= TERMINAL_WINDOW_X + 14 &&
        x < TERMINAL_WINDOW_X + 39 &&
        y >= TERMINAL_WINDOW_Y + 12 &&
        y < TERMINAL_WINDOW_Y + 37;
}

bool desktop_frontend_initialize(
    struct desktop_frontend *desktop,
    struct graphics_context *graphics
)
{
    if (desktop == NULL ||
        graphics == NULL ||
        graphics->info.width != DESKTOP_WIDTH ||
        graphics->info.height != DESKTOP_HEIGHT) {
        return false;
    }

    size_t wallpaper_size =
        (size_t)(
            krishna_wallpaper_end -
            krishna_wallpaper_start
        );

    size_t icon_size =
        (size_t)(
            krishna_terminal_icon_end -
            krishna_terminal_icon_start
        );

    size_t terminal_art_size =
    (size_t)(
        krishna_terminal_art_end -
        krishna_terminal_art_start
    );
    
    size_t expected_wallpaper_size =
        (size_t)DESKTOP_WIDTH *
        (size_t)DESKTOP_HEIGHT *
        4;

    size_t expected_icon_size =
        (size_t)TERMINAL_ICON_WIDTH *
        (size_t)TERMINAL_ICON_HEIGHT *
        4;

    size_t expected_terminal_art_size =
        (size_t)TERMINAL_ART_WIDTH *
        (size_t)TERMINAL_ART_HEIGHT *
        4;

    if (wallpaper_size != expected_wallpaper_size ||
        icon_size != expected_icon_size ||
        terminal_art_size != expected_terminal_art_size) {
        return false;
    }

    if (!font_initialize(&desktop->font)) {
        return false;
    }

    desktop->graphics =
        graphics;

    desktop->wallpaper =
        krishna_wallpaper_start;

    desktop->terminal_icon =
        krishna_terminal_icon_start;

    desktop->terminal_icon_x =
        (
            DESKTOP_WIDTH -
            TERMINAL_ICON_WIDTH
        ) / 2;

    desktop->terminal_icon_y =
        UINT64_C(630);

    desktop->terminal_hovered =
        false;

    desktop->terminal_open =
        false;

    desktop->terminal_input_length = 
        0;

    desktop->terminal_input[0] = 
        '\0';

    return true;
}

void desktop_frontend_render(
    struct desktop_frontend *desktop
)
{
    if (desktop == NULL ||
        desktop->graphics == NULL) {
        return;
    }

    /*
     * This renders into whichever context was supplied during
     * initialization. That should be the desktop backbuffer.
     */
    graphics_blit_rgba(
        desktop->graphics,
        0,
        0,
        desktop->wallpaper,
        DESKTOP_WIDTH,
        DESKTOP_HEIGHT
    );

    draw_top_panel(desktop);
    draw_shelf(desktop);
    draw_terminal_icon(desktop);
    draw_terminal_window(desktop);
}

bool desktop_frontend_update_pointer(
    struct desktop_frontend *desktop,
    uint64_t x,
    uint64_t y
)
{
    if (desktop == NULL) {
        return false;
    }

    bool hovered =
        terminal_icon_contains(
            desktop,
            x,
            y
        );

    if (hovered ==
        desktop->terminal_hovered) {
        return false;
    }

    desktop->terminal_hovered =
        hovered;

    return true;
}

enum desktop_frontend_action
desktop_frontend_click(
    struct desktop_frontend *desktop,
    uint64_t x,
    uint64_t y
)
{
    if (desktop == NULL) {
        return DESKTOP_FRONTEND_ACTION_NONE;
    }

    if (desktop->terminal_open &&
        terminal_close_contains(x, y)) {
        desktop->terminal_open = false;

        return
            DESKTOP_FRONTEND_ACTION_CLOSE_TERMINAL;
    }

    if (!desktop->terminal_open &&
        terminal_icon_contains(
            desktop,
            x,
            y
        )) {
        desktop->terminal_open = true;

        return
            DESKTOP_FRONTEND_ACTION_OPEN_TERMINAL;
    }

    return DESKTOP_FRONTEND_ACTION_NONE;
}

bool desktop_frontend_close_terminal(
    struct desktop_frontend *desktop
)
{
    if (desktop == NULL ||
        !desktop->terminal_open) {
        return false;
    }

    desktop->terminal_open = false;

    return true;
}
bool desktop_frontend_handle_key(
    struct desktop_frontend *desktop,
    const struct krishna_keyboard_event *event
)
{
    if (desktop == NULL ||
        event == NULL ||
        !desktop->terminal_open ||
        event->pressed == 0) {
        return false;
    }

    uint8_t character =
        event->character;

    /*
     * Backspace.
     */
    if (character == '\b') {
        if (desktop->terminal_input_length == 0) {
            return false;
        }

        desktop->terminal_input_length--;

        desktop->terminal_input[
            desktop->terminal_input_length
        ] = '\0';

        return true;
    }

    /*
     * Enter will be forwarded to the real terminal process once that
     * process and its IPC channel exist. For now, preserve the typed
     * line instead of silently deleting it.
     */
    if (character == '\n') {
        return false;
    }

    /*
     * Accept printable ASCII.
     */
    if (character < 32 ||
        character > 126) {
        return false;
    }

    if (desktop->terminal_input_length >=
        DESKTOP_TERMINAL_INPUT_CAPACITY - 1) {
        return false;
    }

    /*
     * Prevent text from reaching the scrollbar/right edge.
     */
    static const char prompt[] =
        "kailash@krishna:~$ ";

    uint64_t prompt_width =
        text_width(
            desktop,
            prompt
        );

    uint64_t existing_width =
        desktop->terminal_input_length *
        desktop->font.glyph_width;

    uint64_t maximum_input_width =
        TERMINAL_WINDOW_WIDTH -
        90 -
        prompt_width;

    if (existing_width +
            desktop->font.glyph_width >
        maximum_input_width) {
        return false;
    }

    desktop->terminal_input[
        desktop->terminal_input_length
    ] = (char)character;

    desktop->terminal_input_length++;

    desktop->terminal_input[
        desktop->terminal_input_length
    ] = '\0';

    return true;
}