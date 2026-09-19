#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>
#include <krishna/device.h>
#include <krishna/graphics.h>
#include <krishna/io.h>
#include <krishna/memory.h>
#include <krishna/process.h>
#include "frontend.h"

#define DESKTOP_WIDTH  UINT64_C(1280)
#define DESKTOP_HEIGHT UINT64_C(800)

#define CURSOR_WIDTH  UINT64_C(20)
#define CURSOR_HEIGHT UINT64_C(28)

#define KRISHNA_LOGO_WIDTH  UINT64_C(627)
#define KRISHNA_LOGO_HEIGHT UINT64_C(527)

extern const uint8_t krishna_logo_start[];
extern const uint8_t krishna_logo_end[];

extern const uint8_t krishna_wallpaper_start[];
extern const uint8_t krishna_wallpaper_end[];

extern const uint8_t krishna_cursor_start[];
extern const uint8_t krishna_cursor_end[];

#define DESKTOP_BACKBUFFER_WIDTH  UINT64_C(1280)
#define DESKTOP_BACKBUFFER_HEIGHT UINT64_C(800)

#define DESKTOP_BACKBUFFER_SIZE \
    (DESKTOP_BACKBUFFER_WIDTH * \
     DESKTOP_BACKBUFFER_HEIGHT * \
     sizeof(uint32_t))

static uint8_t desktop_backbuffer[
    DESKTOP_BACKBUFFER_SIZE
] __attribute__((aligned(16)));

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

static void loading_screen_progress(
    struct graphics_context *graphics,
    uint8_t percentage
)
{
    if (percentage > 100) {
        percentage = 100;
    }

    uint64_t bar_width =
        graphics->info.width / 2;

    if (bar_width > 600) {
        bar_width = 600;
    }

    uint64_t bar_x =
        (
            graphics->info.width -
            bar_width
        ) / 2;

    uint64_t bar_y =
        graphics->info.height - 70;

    uint32_t border =
        graphics_rgb(
            graphics,
            8,
            35,
            55
        );

    uint32_t empty =
        graphics_rgb(
            graphics,
            34,
            91,
            120
        );

    uint32_t filled =
        graphics_rgb(
            graphics,
            250,
            207,
            71
        );

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
        (
            (bar_width - 6) *
            percentage
        ) / 100;

    graphics_rectangle(
        graphics,
        bar_x + 3,
        bar_y + 3,
        completed_width,
        12,
        filled
    );
}

static bool loading_screen_show(
    struct graphics_context *graphics
)
{
    size_t logo_size =
        (size_t)(
            krishna_logo_end -
            krishna_logo_start
        );

    size_t expected_logo_size =
        (size_t)KRISHNA_LOGO_WIDTH *
        (size_t)KRISHNA_LOGO_HEIGHT *
        4;

    if (graphics == NULL ||
        logo_size != expected_logo_size) {
        return false;
    }

    uint32_t background =
        graphics_rgb(
            graphics,
            51,
            170,
            238
        );

    graphics_rectangle(
        graphics,
        0,
        0,
        graphics->info.width,
        graphics->info.height,
        background
    );

    uint64_t logo_x = 0;

    if (graphics->info.width >
        KRISHNA_LOGO_WIDTH) {
        logo_x =
            (
                graphics->info.width -
                KRISHNA_LOGO_WIDTH
            ) / 2;
    }

    uint64_t logo_y = 20;

    graphics_blit_rgba(
        graphics,
        logo_x,
        logo_y,
        krishna_logo_start,
        KRISHNA_LOGO_WIDTH,
        KRISHNA_LOGO_HEIGHT
    );

    loading_screen_progress(
        graphics,
        10
    );

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
    int64_t terminal_process_id = -1;
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

    if (!loading_screen_show(&graphics)) {
        DESKTOP_FAILURE(
            "[DESKTOP] loading screen failed\n"
        );
    }

    (void)krishna_sleep(250);

        if (framebuffer_info.byte_size !=
        sizeof(desktop_backbuffer) ||
        framebuffer_info.pitch !=
            DESKTOP_BACKBUFFER_WIDTH *
            sizeof(uint32_t)) {
        DESKTOP_FAILURE(
            "[DESKTOP] unsupported backbuffer geometry\n"
        );
    }

    struct graphics_context desktop_graphics;

    if (!graphics_init(
            &desktop_graphics,
            desktop_backbuffer,
            &framebuffer_info
        )) {
        DESKTOP_FAILURE(
            "[DESKTOP] backbuffer initialization failed\n"
        );
    }

    loading_screen_progress(
        &graphics,
        40
    );

    (void)krishna_sleep(200);

    struct desktop_frontend desktop;

    if (!desktop_frontend_initialize(
            &desktop,
            &desktop_graphics
        )) {
        DESKTOP_FAILURE(
            "[DESKTOP] frontend initialization failed\n"
        );
    }

    loading_screen_progress(
        &graphics,
        75
    );

    (void)krishna_sleep(200);

    struct desktop_cursor cursor;

    if (!cursor_initialize(&cursor, &graphics)) {
        DESKTOP_FAILURE(
            "[DESKTOP] cursor initialization failed\n"
        );
    }

    bool previous_left_button = false;

    write_message(
        started_message,
        sizeof(started_message) - 1
    );

    loading_screen_progress(
        &graphics,
        100
    );

    /*
    * Hold the completed loading screen long enough to actually see the
    * logo and completed progress bar.
    */
    (void)krishna_sleep(650);

    desktop_frontend_render(&desktop);

    cursor_show(&cursor);

    if (!graphics_present(
        &graphics,
        &desktop_graphics
    )) {
        DESKTOP_FAILURE(
            "[DESKTOP] initial frame presentation failed\n"
        );
    }



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

            bool frontend_changed =
                desktop_frontend_update_pointer(
                    &desktop,
                    cursor.x + 2,
                    cursor.y + 2
                );

            if (frontend_changed) {
                cursor_hide(&cursor);

                desktop_frontend_render(&desktop);

                if (!graphics_present(
                        &graphics,
                        &desktop_graphics
                    )) {
                    DESKTOP_FAILURE(
                        "[DESKTOP] frame presentation failed\n"
                    );
                }

                cursor_show(&cursor);
            }

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

            if (left_clicked) {
                enum desktop_frontend_action action =
                    desktop_frontend_click(
                        &desktop,
                        cursor.x + 2,
                        cursor.y + 2
                    );

                if (action !=
                    DESKTOP_FRONTEND_ACTION_NONE) {
                    cursor_hide(&cursor);

                    desktop_frontend_render(&desktop);

                    if (!graphics_present(
                            &graphics,
                            &desktop_graphics
                        )) {
                        DESKTOP_FAILURE(
                            "[DESKTOP] frame presentation failed\n"
                        );
                    }

                    cursor_show(&cursor);
                }

                if (action ==
                    DESKTOP_FRONTEND_ACTION_OPEN_TERMINAL) {
                    write_message(
                        terminal_message,
                        sizeof(terminal_message) - 1
                    );
                }
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

            bool frontend_changed = false;

            /*
            * Escape remains a desktop/window-manager shortcut.
            */
            if (keyboard_event.pressed &&
                keyboard_event.scancode == 0x01) {
                frontend_changed =
                    desktop_frontend_close_terminal(
                        &desktop
                    );
            } else {
                /*
                * All other keyboard events are offered to the focused
                * terminal frontend.
                */
                frontend_changed =
                    desktop_frontend_handle_key(
                        &desktop,
                        &keyboard_event
                    );
            }

            if (frontend_changed) {
                cursor_hide(&cursor);

                desktop_frontend_render(
                    &desktop
                );

                if (!graphics_present(
                        &graphics,
                        &desktop_graphics
                    )) {
                    DESKTOP_FAILURE(
                        "[DESKTOP] frame presentation failed\n"
                    );
                }

                cursor_show(&cursor);
            }
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