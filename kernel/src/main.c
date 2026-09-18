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
#include "shell.h"
#include "ui/desktop.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/vregion.h"
#include "memory/heap.h"
#include "memory/kernel_pages.h"
#include "memory/kernel_stack.h"
#include "sync/spinlock.h"
#include "task/thread.h"
#include "task/process.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/gdt.h"

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
 * Request Limine's Higher-Half Direct Map.
 *
 * This lets the kernel access physical address P through the
 * virtual address P + hhdm_offset.
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
void kmain(void)
{
    serial_init();

    /*
     * Replace the bootloader's descriptor table with KRISHNA's GDT.
     *
     * This must happen before interrupts_init(), because the IDT
     * records the currently active kernel code selector.
     */
    if (!gdt_init()) {
        serial_write(
            "[FAIL] GDT and TSS initialization failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "\n[OK] GDT and TSS initialized\n"
    );

    /*
     * The IDT now records KRISHNA kernel-code selector, 0x08.
     */
    interrupts_init();

    serial_write(
        "[OK] CPU exception handlers initialized\n"
    );

    serial_write("=====================\n");

    /*
     * Install CPU exception handlers as early as possible.
     */
    interrupts_init();

    serial_write("\nKRISHNA OS early boot\n");
    serial_write("[OK] CPU exception handlers initialized\n");
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

    /*
     * Initialise physical-frame allocation first.
     *
     * The VMM needs the PMM whenever it must allocate another
     * page-table page.
     */
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

    /*
     * Verify that IRQ-safe spinlocks preserve the previous
     * interrupt state.
     */
    spinlock_t test_lock =
        SPINLOCK_INITIALIZER;

    bool interrupts_before =
        interrupts_are_enabled();

    interrupt_state_t test_state =
        spinlock_lock_irqsave(
            &test_lock
        );

    bool disabled_inside =
        !interrupts_are_enabled();

    spinlock_unlock_irqrestore(
        &test_lock,
        test_state
    );

    bool restored_after =
        interrupts_are_enabled() ==
            interrupts_before;

    if (!disabled_inside ||
        !restored_after) {
        serial_write(
            "[FAIL] IRQ-safe spinlock self-test failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] IRQ-safe spinlock self-test passed\n"
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

    struct pmm_statistics before_page_table_clone;
    struct pmm_statistics after_page_table_clone;

    pmm_get_statistics(
        &before_page_table_clone
    );

    if (!vmm_take_ownership()) {
        serial_write(
            "[FAIL] Unable to create KRISHNA-owned page tables\n"
        );

        kernel_halt();
    }

    pmm_get_statistics(
        &after_page_table_clone
    );

    serial_write(
        "[OK] KRISHNA-owned page tables activated\n"
    );

    if (!kernel_vregion_init()) {
        serial_write(
            "[FAIL] Kernel virtual-region allocator initialization failed\n"
        );

        kernel_halt();
    }

    if (!kernel_vregion_self_test()) {
        serial_write(
            "[FAIL] Kernel virtual-region allocator self-test failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Kernel virtual-region allocator initialized\n"
    );

    if (!kernel_pages_self_test()) {
        serial_write(
            "[FAIL] Transactional kernel-page self-test failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Transactional kernel-page self-test passed\n"
    );

    if (!kernel_stack_self_test()) {
        serial_write(
            "[FAIL] Guarded kernel-stack self-test failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Guarded kernel-stack self-test passed\n"
    );

    if (!kheap_init()) {
        serial_write(
            "[FAIL] Kernel heap initialization failed\n"
        );

        kernel_halt();
    }

    if (!kheap_self_test()) {
        serial_write(
            "[FAIL] Kernel heap self-test failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Complete Kernel heap self-test passed\n"
    );

        if (!kernel_process_system_init()) {
        serial_write(
            "[FAIL] Kernel-process system initialization failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Kernel-process system initialized\n"
    );

    if (!kernel_process_self_test()) {
        serial_write(
            "[FAIL] Process address-space self-test failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Process address-space self-test passed\n"
    );

    if (!kernel_thread_system_init()) {
        serial_write(
            "[FAIL] Kernel-thread system initialization failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Kernel-thread system initialized\n"
    );

    if (!kernel_thread_self_test()) {
        serial_write(
            "[FAIL] Cooperative kernel-thread self-test failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Cooperative kernel-thread self-test passed\n"
    );

    if (!kernel_thread_blocking_self_test()) {
        serial_write(
            "[FAIL] Kernel-thread blocking self-test failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Kernel-thread blocking self-test passed\n"
    );

        if (!kernel_thread_process_self_test()) {
        serial_write(
            "[FAIL] Cross-process thread scheduling self-test failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Cross-process thread scheduling self-test passed\n"
    );

    /*
     * Discover the interrupt-controller mode established by the
     * processor and firmware. This operation is read-only.
     */
    struct local_apic_information apic_information;

    if (!local_apic_probe(
            &apic_information
        )) {
        serial_write(
            "[FAIL] Unable to inspect Local APIC configuration\n"
        );

        kernel_halt();
    }

    if (!apic_information.supported) {
        serial_write(
            "[FAIL] Processor does not support a Local APIC\n"
        );

        kernel_halt();
    }

    if (!apic_information.enabled) {
        serial_write(
            "[FAIL] Local APIC is not enabled\n"
        );

        kernel_halt();
    }

    if (apic_information.physical_address == 0 ||
        (apic_information.physical_address &
         (VMM_PAGE_SIZE - 1)) != 0) {
        serial_write(
            "[FAIL] Invalid Local APIC physical address\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Local APIC discovered\n"
    );

    serial_write(
        "Local APIC physical address: "
    );

    serial_write_hex(
        apic_information.physical_address
    );

    serial_write("\nLocal APIC mode: ");

    switch (apic_information.mode) {
        case LOCAL_APIC_MODE_XAPIC:
            serial_write("xAPIC");
            break;

        case LOCAL_APIC_MODE_X2APIC:
            serial_write("x2APIC");
            break;

        default:
            serial_write("disabled");
            break;
    }

    serial_write(
        "\nBootstrap processor: "
    );

    serial_write(
        apic_information.bootstrap_processor
            ? "yes\n"
            : "no\n"
    );

    serial_write(
        "[OK] Local APIC discovery self-test passed\n"
    );

    /*
     * Map the Local APIC hardware page into KRISHNA's dedicated
     * cache-disabled MMIO region.
     */
    if (!local_apic_map(
            &apic_information
        )) {
        serial_write(
            "[FAIL] Unable to map Local APIC MMIO page\n"
        );

        kernel_halt();
    }

    struct local_apic_identity apic_identity;

    if (!local_apic_read_identity(
            &apic_identity
        )) {
        serial_write(
            "[FAIL] Unable to read Local APIC registers\n"
        );

        kernel_halt();
    }

    if (apic_identity.lvt_entry_count < 4) {
        serial_write(
            "[FAIL] Invalid Local APIC LVT information\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Local APIC MMIO page mapped\n"
    );

    serial_write(
        "Local APIC virtual address: "
    );

    serial_write_hex(
        local_apic_virtual_address()
    );

    serial_write(
        "\nLocal APIC ID: "
    );

    serial_write_u64(
        apic_identity.apic_id
    );

    serial_write(
        "\nLocal APIC version: "
    );

    serial_write_hex(
        apic_identity.version
    );

    serial_write(
        "\nLocal APIC LVT entries: "
    );

    serial_write_u64(
        apic_identity.lvt_entry_count
    );

    serial_write(
        "\nLocal APIC software enabled: "
    );

    serial_write(
        apic_identity.software_enabled
            ? "yes\n"
            : "no\n"
    );

    serial_write(
        "[OK] Local APIC MMIO read self-test passed\n"
    );

    /*
     * The Local APIC timer is calibrated using the TSC.
     * Validate the TSC before attempting timer initialization.
     */
    if (tsc_frequency_request.response == NULL ||
        tsc_frequency_request.response->frequency == 0) {
        serial_write(
            "[FAIL] TSC frequency unavailable\n"
        );

        kernel_halt();
    }

    uint64_t tsc_frequency =
        tsc_frequency_request.response->frequency;

    /*
     * Configure the Local APIC timer for a 100 Hz periodic tick.
     * Interrupts remain globally disabled until the IDT gate and
     * all Local APIC registers are ready.
     */
    if (!local_apic_timer_init(
            100,
            tsc_frequency
        )) {
        serial_write(
            "[FAIL] Local APIC timer initialization failed\n"
        );

        kernel_halt();
    }

    /*
     * All interrupt gates and APIC registers are now ready.
     */
    interrupts_enable();

    if (!interrupts_are_enabled()) {
        serial_write(
            "[FAIL] Unable to enable maskable interrupts\n"
        );

        kernel_halt();
    }

    uint64_t timer_test_start_ticks =
        local_apic_timer_ticks();

    uint64_t timer_test_start_tsc =
        read_tsc();

    /*
     * Require at least three timer interrupts within one second.
     */
    while ((local_apic_timer_ticks() -
            timer_test_start_ticks) < 3 &&
           (read_tsc() -
            timer_test_start_tsc) <
                tsc_frequency) {
        __asm__ volatile ("pause");
    }

    if ((local_apic_timer_ticks() -
         timer_test_start_ticks) < 3) {
        serial_write(
            "[FAIL] Local APIC timer self-test failed\n"
        );

        serial_write(
            "Timer ticks observed: "
        );

        serial_write_u64(
            local_apic_timer_ticks() -
            timer_test_start_ticks
        );

        serial_write("\n");
        kernel_halt();
    }

    serial_write(
        "[OK] Local APIC timer initialized at "
    );

    serial_write_u64(
        local_apic_timer_frequency()
    );

    serial_write(" Hz\n");

    serial_write(
        "[OK] Local APIC timer self-test passed\n"
    );

    if (!kernel_thread_enable_preemption()) {
        serial_write(
            "[FAIL] Unable to enable thread preemption\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Kernel-thread preemption enabled\n"
    );

    if (!kernel_thread_timer_self_test()) {
        serial_write(
            "[FAIL] Preemptive scheduler self-test failed\n"
        );

        kernel_halt();
    }

    serial_write(
        "[OK] Preemptive scheduler self-test passed\n"
    );

    struct kheap_statistics heap_statistics;

    kheap_get_statistics(
        &heap_statistics
    );

    serial_write(
        "[OK] Kernel heap arena created\n"
    );

    serial_write(
        "Kernel heap mapped pages: "
    );

    serial_write_u64(
        heap_statistics.mapped_pages
    );

    serial_write(
        "\nKernel heap free bytes: "
    );

    serial_write_u64(
        heap_statistics.free_bytes
    );

    serial_write("\n");

    serial_write(
        "Page-table frames cloned: "
    );

    serial_write_u64(
        before_page_table_clone.free_pages -
        after_page_table_clone.free_pages
    );

    serial_write("\n");

    struct pmm_statistics pmm_stats;

    pmm_get_statistics(
        &pmm_stats
    );

    serial_write(
        "Managed physical pages: "
    );

    serial_write_u64(
        pmm_stats.managed_pages
    );

    serial_write(
        "\nFree physical pages: "
    );

    serial_write_u64(
        pmm_stats.free_pages
    );

    serial_write(
        "\nPaging metadata pages: "
    );

    serial_write_u64(
        pmm_stats.bitmap_pages
    );

    serial_write(
        "\nNX protection: "
    );

    if (vmm_nx_supported()) {
        serial_write("supported\n");
    } else {
        serial_write("unavailable\n");
    }

    splash_set_progress(
        &graphics,
        100
    );

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

    uint64_t cursor_interval =
        local_apic_timer_frequency() / 2;

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

    desktop_render(
        &desktop
    );

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

    mouse_cursor_show(
        &pointer
    );

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
        local_apic_timer_ticks() + cursor_interval;

    bool previous_left_button =
        false;

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

        kernel_thread_preemption_point();

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
                mouse_cursor_hide(
                    &pointer
                );

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

                mode =
                    INTERFACE_TERMINAL;

                next_cursor_toggle =
                    local_apic_timer_ticks() +
                    cursor_interval;

                mouse_cursor_show(
                    &pointer
                );

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
                mouse_cursor_hide(
                    &pointer
                );

                desktop_render(
                    &desktop
                );

                mode =
                    INTERFACE_DESKTOP;

                mouse_cursor_show(
                    &pointer
                );

                serial_write(
                    "[OK] Returned to desktop\n"
                );
            } else if (
                key_event.character != '\0'
            ) {
                mouse_cursor_hide(
                    &pointer
                );

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
                    local_apic_timer_ticks() +
                    cursor_interval;

                mouse_cursor_show(
                    &pointer
                );
            }
        }

        /*
         * Blink the text cursor only while the terminal is open.
         */
        if (mode == INTERFACE_TERMINAL) {
            uint64_t now =
                local_apic_timer_ticks();

            if ((int64_t)(
                    now -
                    next_cursor_toggle
                ) >= 0) {
                mouse_cursor_hide(
                    &pointer
                );

                console_set_cursor_visible(
                    &console,
                    !console.cursor_visible
                );

                mouse_cursor_show(
                    &pointer
                );

                next_cursor_toggle =
                    now +
                    cursor_interval;
            }
        }

        __asm__ volatile ("pause");
    }
}