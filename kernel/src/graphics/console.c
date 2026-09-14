#include "graphics/console.h"

struct __attribute__((packed)) kfont_header {
    char magic[8];
    uint16_t glyph_width;
    uint16_t glyph_height;
    uint16_t first_character;
    uint16_t glyph_count;
};

static bool font_magic_valid(const char magic[8])
{
    static const char expected[8] = {
        'K', 'F', 'O', 'N', 'T', '0', '0', '1'
    };

    for (uint8_t i = 0; i < 8; i++) {
        if (magic[i] != expected[i]) {
            return false;
        }
    }

    return true;
}

static void console_scroll(struct console *console)
{
    struct graphics_context *graphics = console->graphics;
    uint64_t width = graphics->framebuffer->width;
    uint64_t height = graphics->framebuffer->height;

    uint64_t first_y = console->margin;
    uint64_t last_y = height - console->margin;

    for (uint64_t y = first_y;
         y + console->line_height < last_y;
         y++) {
        for (uint64_t x = console->margin;
             x < width - console->margin;
             x++) {
            graphics->pixels[y * graphics->stride + x] =
                graphics->pixels[
                    (y + console->line_height)
                    * graphics->stride + x
                ];
        }
    }

    graphics_rectangle(
        graphics,
        console->margin,
        last_y - console->line_height,
        width - (console->margin * 2),
        console->line_height,
        console->background
    );
}

static void console_newline(struct console *console)
{
    console->cursor_x = console->margin;
    console->cursor_y += console->line_height;

    uint64_t bottom =
        console->graphics->framebuffer->height - console->margin;

    if (console->cursor_y + console->glyph_height >= bottom) {
        console_scroll(console);
        console->cursor_y -= console->line_height;
    }
}

static void console_backspace(struct console *console)
{
    uint64_t cell_width =
        console->glyph_width + console->spacing;

    if (console->cursor_x <= console->margin) {
        return;
    }

    console->cursor_x -= cell_width;

    graphics_rectangle(
        console->graphics,
        console->cursor_x,
        console->cursor_y,
        cell_width,
        console->line_height,
        console->background
    );
}

bool console_init(
    struct console *console,
    struct graphics_context *graphics,
    const uint8_t *font_file,
    size_t font_file_size
)
{
    if (font_file == NULL ||
        font_file_size < sizeof(struct kfont_header)) {
        return false;
    }

    const struct kfont_header *header =
        (const struct kfont_header *)font_file;

    if (!font_magic_valid(header->magic)) {
        return false;
    }

    size_t required_size =
        sizeof(struct kfont_header) +
        ((size_t)header->glyph_width
         * header->glyph_height
         * header->glyph_count);

    if (font_file_size != required_size) {
        return false;
    }

    console->graphics = graphics;
    console->glyph_data =
        font_file + sizeof(struct kfont_header);

    console->glyph_width = header->glyph_width;
    console->glyph_height = header->glyph_height;
    console->first_character = header->first_character;
    console->glyph_count = header->glyph_count;

    console->margin = 24;
    console->spacing = 1;
    console->line_height = console->glyph_height + 2;

    console->background =
        graphics_rgb(graphics, 11, 18, 32);

    console->foreground =
        graphics_rgb(graphics, 220, 245, 250);

    console->cursor_colour = 
        graphics_rgb(graphics, 250, 207, 71);

    console->cursor_visible = false;

    console_clear(console);
    return true;
}

void console_clear(struct console *console)
{
    graphics_clear(
        console->graphics,
        console->background
    );

    console->cursor_x = console->margin;
    console->cursor_y = console->margin;
}

void console_put_character(
    struct console *console,
    char character
)
{
    console_hide_cursor(console);
    if (character == '\n') {
        console_newline(console);
        console_show_cursor(console);
        return;
    }

    if (character == '\r') {
        console->cursor_x = console->margin;
        console_show_cursor(console);
        return;
    }

    if (character == '\b') {
        console_backspace(console);
        console_show_cursor(console);
        return;
    }

    if (character == '\t') {
        for (uint8_t i = 0; i < 4; i++) {
            console_put_character(console, ' ');
        }
        console_show_cursor(console);
        return;
    }

    uint8_t code = (uint8_t)character;

    uint16_t last_character =
        console->first_character + console->glyph_count;

    if (code < console->first_character ||
        code >= last_character) {
        code = '?';
    }

    uint64_t cell_width =
        console->glyph_width + console->spacing;

    uint64_t right_edge =
        console->graphics->framebuffer->width - console->margin;

    if (console->cursor_x + cell_width >= right_edge) {
        console_newline(console);
    }

    graphics_rectangle(
        console->graphics,
        console->cursor_x,
        console->cursor_y,
        cell_width,
        console->line_height,
        console->background
    );

    uint64_t glyph_index =
        code - console->first_character;

    const uint8_t *glyph =
        console->glyph_data +
        glyph_index *
        console->glyph_width *
        console->glyph_height;

    for (uint16_t y = 0; y < console->glyph_height; y++) {
        for (uint16_t x = 0; x < console->glyph_width; x++) {
            uint8_t alpha =
                glyph[y * console->glyph_width + x];

            graphics_blend_pixel(
                console->graphics,
                console->cursor_x + x,
                console->cursor_y + y,
                220,
                245,
                250,
                alpha
            );
        }
    }

    console->cursor_x += cell_width;
    console_show_cursor(console);
}

void console_write(
    struct console *console,
    const char *text
)
{
    while (*text != '\0') {
        console_put_character(console, *text);
        text++;
    }
}

void console_write_u64(
    struct console *console,
    uint64_t value
)
{
    char digits[20];
    uint8_t count = 0;

    if (value == 0) {
        console_put_character(console, '0');
        return;
    }

    while (value != 0) {
        digits[count++] =
            (char)('0' + value % 10);

        value /= 10;
    }

    while (count != 0) {
        console_put_character(
            console,
            digits[--count]
        );
    }
}

static void console_hide_cursor(struct console *console) {
    if (!console->cursor_visible) {
        return;
    }

    graphics_rectangle(
        console->graphics,
        console->cursor_x,
        console->cursor_y + console->glyph_height,
        console->glyph_width,
        2,
        console->background
    );

    console->cursor_visible = false;
}

static void console_show_cursor(struct console *console) {
    graphics_rectangle(
        console->graphics,
        console->cursor_x,
        console->cursor_y + console->glyph_height,
        console->glyph_width,
        2,
        console->cursor_colour
    );

    console->cursor_visible = true;
}

void console_set_cursor_visible(
    struct console *console,
    bool visible
) {
    if (visible) {
        console_show_cursor(console);
    } else {
        console_hide_cursor(console);
    }
}