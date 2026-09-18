#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>
#include <krishna/device.h>
#include <krishna/graphics.h>
#include <krishna/io.h>
#include <krishna/memory.h>
#include <krishna/process.h>

#define DESKTOP_WIDTH  UINT64_C(1280)
#define DESKTOP_HEIGHT UINT64_C(800)

#define CURSOR_WIDTH  UINT64_C(20)
#define CURSOR_HEIGHT UINT64_C(28)

extern const uint8_t krishna_wallpaper_start[];
extern const uint8_t krishna_wallpaper_end[];

extern const uint8_t krishna_cursor_start[];
extern const uint8_t krishna_cursor_end[];

struct desktop {
    struct graphics_context *graphics;

    uint64_t terminal_x;
    uint64_t terminal_y;
    uint64_t terminal_width;
    uint64_t terminal_height;
};

struct desktop_cursor {
    struct graphics_context *graphics;

    const uint8_t *image;

    uint64_t x;
    uint64_t y;

    uint32_t saved_background[
        CURSOR_WIDTH * CURSOR_HEIGHT
    ];

    bool visible;
};

static _Noreturn void desktop_failure(
    const char *message,
    size_t length
)
{
    (void)krishna_write(
        KRISHNA_STDERR,
        message,
        length
    );

    /*
     * Keep the failed desktop alive so _start does not call exit and
     * eventually reach its defensive UD2 instruction.
     */
    for (;;) {
        (void)krishna_yield();
    }
}

#define DESKTOP_FAILURE(message) \
    desktop_failure((message), sizeof(message) - 1)

static void desktop_glass_panel(
    struct graphics_context *graphics,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint64_t radius
)
{
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

    if (width > 2 && height > 2) {
        graphics_rounded_rectangle(
            graphics,
            x + 1,
            y + 1,
            width - 2,
            height - 2,
            radius > 0
                ? radius - 1
                : 0,
            5,
            30,
            58,
            205
        );
    }
}

static void desktop_draw_terminal_icon(
    struct desktop *desktop
)
{
    struct graphics_context *graphics =
        desktop->graphics;

    uint64_t x = desktop->terminal_x;
    uint64_t y = desktop->terminal_y;

    graphics_rounded_rectangle(
        graphics,
        x - 2,
        y - 2,
        desktop->terminal_width + 4,
        desktop->terminal_height + 4,
        14,
        250,
        199,
        55,
        255
    );

    graphics_rounded_rectangle(
        graphics,
        x,
        y,
        desktop->terminal_width,
        desktop->terminal_height,
        12,
        5,
        24,
        45,
        245
    );

    uint32_t symbol_colour =
        graphics_rgb(
            graphics,
            220,
            250,
            255
        );

    for (uint64_t index = 0;
         index <= 10;
         index++) {
        graphics_rectangle(
            graphics,
            x + 17 + index,
            y + 16 + index,
            3,
            3,
            symbol_colour
        );

        graphics_rectangle(
            graphics,
            x + 27 - index,
            y + 26 + index,
            3,
            3,
            symbol_colour
        );
    }

    graphics_rectangle(
        graphics,
        x + 35,
        y + 36,
        14,
        3,
        symbol_colour
    );

    graphics_rounded_rectangle(
        graphics,
        x + 12,
        y + desktop->terminal_height + 5,
        40,
        4,
        2,
        250,
        199,
        55,
        255
    );
}

static bool desktop_initialize(
    struct desktop *desktop,
    struct graphics_context *graphics
)
{
    if (desktop == NULL ||
        graphics == NULL ||
        graphics->info.width !=
            DESKTOP_WIDTH ||
        graphics->info.height !=
            DESKTOP_HEIGHT) {
        return false;
    }

    desktop->graphics = graphics;

    desktop->terminal_width = 64;
    desktop->terminal_height = 64;

    desktop->terminal_x =
        (
            DESKTOP_WIDTH -
            desktop->terminal_width
        ) / 2;

    desktop->terminal_y =
        DESKTOP_HEIGHT -
        92 -
        26 +
        14;

    return true;
}

static void desktop_render(
    struct desktop *desktop
)
{
    struct graphics_context *graphics =
        desktop->graphics;

    graphics_blit_rgba(
        graphics,
        0,
        0,
        krishna_wallpaper_start,
        DESKTOP_WIDTH,
        DESKTOP_HEIGHT
    );

    desktop_glass_panel(
        graphics,
        0,
        0,
        DESKTOP_WIDTH,
        52,
        18
    );

    const uint64_t dock_width = 480;
    const uint64_t dock_height = 92;

    desktop_glass_panel(
        graphics,
        (DESKTOP_WIDTH - dock_width) / 2,
        DESKTOP_HEIGHT -
            dock_height -
            26,
        dock_width,
        dock_height,
        24
    );

    desktop_draw_terminal_icon(desktop);
}

static bool desktop_terminal_contains(
    const struct desktop *desktop,
    uint64_t x,
    uint64_t y
)
{
    return
        x >= desktop->terminal_x &&
        y >= desktop->terminal_y &&
        x <
            desktop->terminal_x +
            desktop->terminal_width &&
        y <
            desktop->terminal_y +
            desktop->terminal_height;
}

static bool cursor_initialize(
    struct desktop_cursor *cursor,
    struct graphics_context *graphics
)
{
    size_t image_size =
        (size_t)(
            krishna_cursor_end -
            krishna_cursor_start
        );

    if (cursor == NULL ||
        graphics == NULL ||
        image_size !=
            CURSOR_WIDTH *
            CURSOR_HEIGHT *
            4) {
        return false;
    }

    cursor->graphics = graphics;
    cursor->image =
        krishna_cursor_start;

    cursor->x =
        (
            graphics->info.width -
            CURSOR_WIDTH
        ) / 2;

    cursor->y =
        (
            graphics->info.height -
            CURSOR_HEIGHT
        ) / 2;

    cursor->visible = false;

    return true;
}

static void cursor_hide(
    struct desktop_cursor *cursor
)
{
    if (!cursor->visible) {
        return;
    }

    for (uint64_t y = 0;
         y < CURSOR_HEIGHT;
         y++) {
        for (uint64_t x = 0;
             x < CURSOR_WIDTH;
             x++) {
            uint64_t index =
                y * CURSOR_WIDTH + x;

            graphics_set_pixel(
                cursor->graphics,
                cursor->x + x,
                cursor->y + y,
                cursor->saved_background[
                    index
                ]
            );
        }
    }

    cursor->visible = false;
}

static void cursor_show(
    struct desktop_cursor *cursor
)
{
    if (cursor->visible) {
        return;
    }

    for (uint64_t y = 0;
         y < CURSOR_HEIGHT;
         y++) {
        for (uint64_t x = 0;
             x < CURSOR_WIDTH;
             x++) {
            uint64_t index =
                y * CURSOR_WIDTH + x;

            cursor->saved_background[index] =
                graphics_get_pixel(
                    cursor->graphics,
                    cursor->x + x,
                    cursor->y + y
                );
        }
    }

    graphics_blit_rgba(
        cursor->graphics,
        cursor->x,
        cursor->y,
        cursor->image,
        CURSOR_WIDTH,
        CURSOR_HEIGHT
    );

    cursor->visible = true;
}

static void cursor_move(
    struct desktop_cursor *cursor,
    int16_t delta_x,
    int16_t delta_y
)
{
    if (delta_x == 0 &&
        delta_y == 0) {
        return;
    }

    cursor_hide(cursor);

    int64_t new_x =
        (int64_t)cursor->x +
        delta_x;

    int64_t new_y =
        (int64_t)cursor->y +
        delta_y;

    int64_t maximum_x =
        (int64_t)
            cursor->graphics->info.width -
        (int64_t)CURSOR_WIDTH;

    int64_t maximum_y =
        (int64_t)
            cursor->graphics->info.height -
        (int64_t)CURSOR_HEIGHT;

    if (new_x < 0) {
        new_x = 0;
    }

    if (new_y < 0) {
        new_y = 0;
    }

    if (new_x > maximum_x) {
        new_x = maximum_x;
    }

    if (new_y > maximum_y) {
        new_y = maximum_y;
    }

    cursor->x = (uint64_t)new_x;
    cursor->y = (uint64_t)new_y;

    cursor_show(cursor);
}

static void write_message(
    const char *message,
    size_t size
)
{
    (void)krishna_write(
        KRISHNA_STDOUT,
        message,
        size
    );
}

int main(void)
{
    static const char started_message[] =
        "[DESKTOP] Ring-3 desktop started\n";

    static const char terminal_message[] =
        "[DESKTOP] Terminal launch requested\n";

    struct krishna_framebuffer_info
        framebuffer_info;

    int64_t information_result =
        krishna_ioctl(
            KRISHNA_HANDLE_FRAMEBUFFER,
            KRISHNA_FRAMEBUFFER_IOCTL_GET_INFO,
            &framebuffer_info,
            sizeof(framebuffer_info)
        );

    if (information_result !=
        (int64_t)sizeof(framebuffer_info)) {
        DESKTOP_FAILURE(
            "[DESKTOP] framebuffer GET_INFO failed\n"
        );
    }

    struct graphics_context graphics;

    int64_t framebuffer_mapping =
        krishna_memory_map(
            KRISHNA_HANDLE_FRAMEBUFFER,
            NULL,
            (size_t)framebuffer_info.byte_size,
            0,
            KRISHNA_MEMORY_PROTECTION_READ |
                KRISHNA_MEMORY_PROTECTION_WRITE,
            0
        );

    if (framebuffer_mapping < 0) {
        DESKTOP_FAILURE(
            "[DESKTOP] framebuffer mapping failed\n"
        );
    }

    if (!graphics_init(
            &graphics,
            (void *)(uintptr_t)framebuffer_mapping,
            &framebuffer_info
        )) {
        DESKTOP_FAILURE(
            "[DESKTOP] graphics initialization failed\n"
        );
    }

    size_t wallpaper_size =
        (size_t)(
            krishna_wallpaper_end -
            krishna_wallpaper_start
        );

    if (wallpaper_size !=
        DESKTOP_WIDTH * DESKTOP_HEIGHT * 4) {
        DESKTOP_FAILURE(
            "[DESKTOP] wallpaper size mismatch\n"
        );
    }

    struct desktop desktop;

    if (!desktop_initialize(&desktop, &graphics)) {
        DESKTOP_FAILURE(
            "[DESKTOP] desktop initialization failed\n"
        );
    }

    desktop_render(&desktop);

    struct desktop_cursor cursor;

    if (!cursor_initialize(&cursor, &graphics)) {
        DESKTOP_FAILURE(
            "[DESKTOP] cursor initialization failed\n"
        );
    }

    cursor_show(&cursor);

    bool previous_left_button = false;

    write_message(
        started_message,
        sizeof(started_message) - 1
    );

    for (;;) {
        bool handled_event = false;

        struct krishna_mouse_event
            mouse_event;

        int64_t mouse_result =
            krishna_read(
                KRISHNA_HANDLE_MOUSE,
                &mouse_event,
                sizeof(mouse_event)
            );

        if (mouse_result ==
            (int64_t)sizeof(mouse_event)) {
            handled_event = true;

            cursor_move(
                &cursor,
                mouse_event.delta_x,
                mouse_event.delta_y
            );

            bool left_button =
                (
                    mouse_event.buttons &
                    KRISHNA_MOUSE_BUTTON_LEFT
                ) != 0;

            bool left_clicked =
                left_button &&
                !previous_left_button;

            previous_left_button =
                left_button;

            if (left_clicked &&
                desktop_terminal_contains(
                    &desktop,
                    cursor.x + 2,
                    cursor.y + 2
                )) {
                write_message(
                    terminal_message,
                    sizeof(terminal_message) - 1
                );
            }
        } else if (
            mouse_result !=
                -KRISHNA_ERROR_WOULD_BLOCK
        ) {
            DESKTOP_FAILURE(
                "[DESKTOP] mouse read failed\n"
            );
        }

        struct krishna_keyboard_event
            keyboard_event;

        int64_t keyboard_result =
            krishna_read(
                KRISHNA_HANDLE_KEYBOARD,
                &keyboard_event,
                sizeof(keyboard_event)
            );

        if (keyboard_result ==
            (int64_t)sizeof(
                keyboard_event
            )) {
            handled_event = true;
        } else if (
            keyboard_result !=
                -KRISHNA_ERROR_WOULD_BLOCK
        ) {
            DESKTOP_FAILURE(
                "[DESKTOP] keyboard read failed\n"
            );
        }

        /*
         * Until poll() exists, yield whenever there is no work.
         */
        if (!handled_event) {
            (void)krishna_yield();
        }
    }
}