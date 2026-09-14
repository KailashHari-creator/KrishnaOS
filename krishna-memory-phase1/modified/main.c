#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "drivers/serial.h"
#include "drivers/mouse.h"
#include "drivers/keyboard.h"
#include "boot/boot_info.h"
#include "graphics/graphics.h"
#include "graphics/splash.h"
#include "graphics/console.h"
#include "graphics/mouse_cursor.h"
#include "interrupts.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "shell.h"
#include "ui/desktop.h"

/*
 * Tell Limine which base protocol revision our kernel expects.
 */
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

/*
 * Ask Limine to provide a graphical framebuffer.
 */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

/*
 * Ask Limine for the physical-memory map.
 */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

/*
 * Ask Limine for the higher-half direct map. This gives the kernel a stable
 * virtual address for every physical frame managed by the PMM.
 */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_tsc_frequency_request
tsc_frequency_request = {
    .id = LIMINE_TSC_FREQUENCY_REQUEST_ID,
    .revision = 0
};

/*
 * These markers identify the beginning and end of our Limine requests.
 */
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

/*
 * Stop this CPU permanently.
 */
static void kernel_halt(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static bool text_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return false;
        }

        left++;
        right++;
    }

    return *left == *right;
}

static struct limine_file *find_module(const char *name)
{
    if (module_request.response == NULL) {
        return NULL;
    }

    for (uint64_t i = 0;
         i < module_request.response->module_count;
         i++) {
        struct limine_file *module =
            module_request.response->modules[i];

        if (module->string != NULL &&
            text_equal(module->string, name)) {
            return module;
        }
    }

    return NULL;
}

static uint64_t read_tsc(void)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile (
        "rdtsc"
        : "=a"(low), "=d"(high)
    );

    return ((uint64_t)high << 32) | low;
}

/*
 * Kernel entry point.
 * Limine transfers CPU execution here after loading the kernel.
 */
/*
 * Kernel entry point.
 * Limine transfers CPU execution here after loading the kernel.
 */
void kmain(void)
{
    serial_init();
    serial_write("\nKRISHNA OS early boot\n");
    serial_write("=====================\n");

    /*
     * Validate the Limine protocol revision.
     */
    if (!LIMINE_BASE_REVISION_SUPPORTED(
            limine_base_revision
        )) {
        serial_write(
            "[FAIL] Unsupported Limine protocol revision\n"
        );

        kernel_halt();
    }

    serial_write("[OK] Limine protocol handoff\n");

    /*
     * Obtain the primary framebuffer.
     */
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response
            ->framebuffer_count == 0) {
        serial_write("[FAIL] No framebuffer received\n");
        kernel_halt();
    }

    struct limine_framebuffer *framebuffer =
        framebuffer_request.response
            ->framebuffers[0];

    if (framebuffer->memory_model !=
            LIMINE_FRAMEBUFFER_RGB ||
        framebuffer->bpp != 32 ||
        framebuffer->red_mask_size != 8 ||
        framebuffer->green_mask_size != 8 ||
        framebuffer->blue_mask_size != 8) {
        serial_write(
            "[FAIL] Unsupported framebuffer format\n"
        );

        kernel_halt();
    }

    struct graphics_context graphics;
    graphics_init(&graphics, framebuffer);

    serial_write("[OK] Framebuffer: ");
    serial_write_u64(framebuffer->width);
    serial_write(" x ");
    serial_write_u64(framebuffer->height);
    serial_write(" x ");
    serial_write_u64(framebuffer->bpp);
    serial_write("\n");

    /*
     * Ensure Limine supplied our boot modules.
     */
    if (module_request.response == NULL ||
        module_request.response->module_count == 0) {
        serial_write("[FAIL] No boot modules loaded\n");
        kernel_halt();
    }

    /*
     * Find and display the boot logo.
     */
    struct limine_file *logo_module =
        find_module("krishna-logo");

    if (logo_module == NULL) {
        serial_write(
            "[FAIL] KRISHNA OS logo module missing\n"
        );

        kernel_halt();
    }

    if (!splash_show(
            &graphics,
            (const uint8_t *)logo_module->address,
            logo_module->size
        )) {
        serial_write("[FAIL] Invalid KRISHNA OS logo\n");
        kernel_halt();
    }

    splash_set_progress(&graphics, 30);

    serial_write(
        "[OK] KRISHNA OS splash displayed\n"
    );

    /*
     * Validate and analyse physical memory.
     */
    if (memory_map_request.response == NULL) {
        serial_write(
            "[FAIL] No physical-memory map received\n"
        );

        kernel_halt();
    }

    struct boot_memory_summary memory;

    boot_analyse_memory(
        memory_map_request.response,
        &memory
    );

    serial_write(
        "[OK] Physical-memory map received\n"
    );

    serial_write("\nMemory regions: ");
    serial_write_u64(memory.region_count);

    serial_write("\nUsable regions: ");
    serial_write_u64(
        memory.usable_region_count
    );

    serial_write("\nUsable memory: ");
    serial_write_u64(
        memory.usable_bytes >> 20
    );
    serial_write(" MiB");

    splash_set_progress(&graphics, 65);

    serial_write(
        "\nBootloader-reclaimable memory: "
    );

    serial_write_u64(
        memory.bootloader_reclaimable_bytes >> 20
    );

    serial_write(" MiB\n");

    if (memory.usable_bytes == 0) {
        serial_write(
            "[FAIL] No usable physical memory\n"
        );

        kernel_halt();
    }

    if (hhdm_request.response == NULL) {
        serial_write(
            "[FAIL] No higher-half direct map received\n"
        );
        kernel_halt();
    }

    uint64_t hhdm_offset =
        hhdm_request.response->offset;

    if (!pmm_init(
            memory_map_request.response,
            hhdm_offset
        )) {
        serial_write(
            "[FAIL] Physical-memory manager initialization failed\n"
        );
        kernel_halt();
    }

    serial_write(
        "[OK] Physical-memory manager initialized\n"
    );

    if (!vmm_init(hhdm_offset)) {
        serial_write(
            "[FAIL] Virtual-memory manager initialization failed\n"
        );
        kernel_halt();
    }

    serial_write(
        "[OK] Four-level x86-64 paging initialized\n"
    );

    struct pmm_statistics pmm_stats;
    pmm_get_statistics(&pmm_stats);

    serial_write("Managed physical pages: ");
    serial_write_u64(pmm_stats.managed_pages);
    serial_write("\nFree physical pages: ");
    serial_write_u64(pmm_stats.free_pages);
    serial_write("\nPaging metadata pages: ");
    serial_write_u64(pmm_stats.bitmap_pages);
    serial_write("\nNX protection: ");
    serial_write(
        vmm_nx_supported() ?
            "supported\n" : "unavailable\n"
    );

    splash_set_progress(&graphics, 100);

    serial_write(
        "\n[OK] Early boot environment validated\n"
    );

    serial_write(
        "[OK] Waiting at KRISHNA OS splash\n"
    );

    serial_write(
        "Press any key inside QEMU to continue\n"
    );

    /*
     * Keep the completed splash visible until a key is pressed.
     */
    struct key_event key_event;

    for (;;) {
        if (keyboard_poll(&key_event) &&
            key_event.pressed) {
            break;
        }

        __asm__ volatile ("pause");
    }

    /*
     * Locate the desktop assets.
     */
    struct limine_file *wallpaper_module =
        find_module("krishna-wallpaper");

    if (wallpaper_module == NULL) {
        serial_write(
            "[FAIL] KRISHNA wallpaper missing\n"
        );

        kernel_halt();
    }

    struct limine_file *cursor_module =
        find_module("krishna-cursor");

    if (cursor_module == NULL) {
        serial_write(
            "[FAIL] KRISHNA cursor module missing\n"
        );

        kernel_halt();
    }

    struct limine_file *font_module =
        find_module("krishna-font");

    if (font_module == NULL) {
        serial_write(
            "[FAIL] KRISHNA OS font module missing\n"
        );

        kernel_halt();
    }

    if (tsc_frequency_request.response == NULL ||
        tsc_frequency_request.response->frequency == 0) {
        serial_write(
            "[FAIL] TSC frequency unavailable\n"
        );

        kernel_halt();
    }

    uint64_t tsc_frequency =
        tsc_frequency_request.response->frequency;

    uint64_t cursor_interval =
        tsc_frequency / 2;

    /*
     * Initialize the PS/2 mouse before entering desktop mode.
     */
    if (!mouse_init()) {
        serial_write(
            "[FAIL] PS/2 mouse initialization failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] PS/2 mouse polling enabled\n"
    );

    /*
     * Validate and render the desktop.
     */
    struct desktop desktop;

    if (!desktop_init(
            &desktop,
            &graphics,
            (const uint8_t *)
                wallpaper_module->address,
            wallpaper_module->size
        )) {
        serial_write(
            "[FAIL] Invalid desktop wallpaper\n"
        );

        kernel_halt();
    }

    desktop_render(&desktop);

    serial_write(
        "[OK] KRISHNA desktop rendered\n"
    );

    /*
     * Initialize the mouse pointer only after the desktop has
     * finished drawing. This ensures it saves the correct
     * desktop pixels underneath itself.
     */
    struct mouse_cursor pointer;

    if (!mouse_cursor_init(
            &pointer,
            &graphics,
            (const uint8_t *)
                cursor_module->address,
            cursor_module->size
        )) {
        serial_write(
            "[FAIL] Invalid KRISHNA cursor asset\n"
        );

        kernel_halt();
    }

    mouse_cursor_show(&pointer);

    serial_write(
        "[OK] KRISHNA pointer displayed\n"
    );

    enum interface_mode {
        INTERFACE_DESKTOP,
        INTERFACE_TERMINAL
    };

    enum interface_mode mode =
        INTERFACE_DESKTOP;

    struct console console;
    struct shell shell;

    uint64_t next_cursor_toggle =
        read_tsc() + cursor_interval;

    bool previous_left_button = false;

    /*
     * KRISHNA OS desktop event loop.
     *
     * Currently this handles mouse movement only.
     * Terminal-icon hit testing will be added next.
     */
    struct mouse_event mouse_event;

    /*
    * KRISHNA OS desktop event loop.
    *
    * Both PS/2 devices must be polled because they share the
    * controller output buffer.
    */
    for (;;) {
        bool key_available =
            keyboard_poll(&key_event);

        bool mouse_available =
            mouse_poll(&mouse_event);

        /*
        * Move the pointer in either interface mode.
        */
        if (mouse_available) {
            mouse_cursor_move(
                &pointer,
                mouse_event.delta_x,
                mouse_event.delta_y
            );

            /*
            * Detect only the transition from released to pressed.
            * This prevents one click from launching repeatedly.
            */
            bool left_clicked =
                mouse_event.left_button &&
                !previous_left_button;

            previous_left_button =
                mouse_event.left_button;

            if (mode == INTERFACE_DESKTOP &&
                left_clicked &&
                desktop_terminal_contains(
                    &desktop,
                    pointer.x + 2,
                    pointer.y + 2
                )) {

                /*
                * Remove the pointer before replacing the desktop.
                */
                mouse_cursor_hide(&pointer);

                if (!console_init(
                        &console,
                        &graphics,
                        (const uint8_t *)
                            font_module->address,
                        font_module->size
                    )) {
                    serial_write(
                        "[FAIL] Unable to open terminal\n"
                    );

                    kernel_halt();
                }

                console_write(
                    &console,
                    "KRISHNA TERMINAL\n"
                );

                console_write(
                    &console,
                    "================\n\n"
                );

                console_write(
                    &console,
                    "Welcome to KRISHNA OS.\n"
                );

                console_write(
                    &console,
                    "Press Escape to return to the desktop.\n\n"
                );

                shell_init(
                    &shell,
                    &console,
                    memory.usable_bytes
                );

                console_set_cursor_visible(
                    &console,
                    true
                );

                mode = INTERFACE_TERMINAL;

                next_cursor_toggle =
                    read_tsc() + cursor_interval;

                mouse_cursor_show(&pointer);

                serial_write(
                    "[OK] Terminal application opened\n"
                );
            }
        }

        /*
        * Terminal keyboard handling.
        */
        if (mode == INTERFACE_TERMINAL &&
            key_available &&
            key_event.pressed) {

            /*
            * Escape scancode in PS/2 Set 1 is 0x01.
            */
            if (key_event.scancode == 0x01) {
                mouse_cursor_hide(&pointer);

                desktop_render(&desktop);

                mode = INTERFACE_DESKTOP;

                mouse_cursor_show(&pointer);

                serial_write(
                    "[OK] Returned to desktop\n"
                );
            } else if (key_event.character != '\0') {
                mouse_cursor_hide(&pointer);

                /*
                * Preserve the serial developer mirror.
                */
                if (key_event.character == '\b') {
                    serial_write("\b \b");
                } else {
                    serial_write_character(
                        key_event.character
                    );
                }

                shell_handle_character(
                    &shell,
                    key_event.character
                );

                console_set_cursor_visible(
                    &console,
                    true
                );

                next_cursor_toggle =
                    read_tsc() + cursor_interval;

                mouse_cursor_show(&pointer);
            }
        }

        /*
        * Blink the text cursor only while the terminal is open.
        */
        if (mode == INTERFACE_TERMINAL) {
            uint64_t now = read_tsc();

            if ((int64_t)(
                    now - next_cursor_toggle
                ) >= 0) {
                mouse_cursor_hide(&pointer);

                console_set_cursor_visible(
                    &console,
                    !console.cursor_visible
                );

                mouse_cursor_show(&pointer);

                next_cursor_toggle =
                    now + cursor_interval;
            }
        }

        __asm__ volatile ("pause");
    }
}
