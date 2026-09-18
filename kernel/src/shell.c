#include "shell.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "memory/memory_test.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "drivers/serial.h"

#define SHELL_PROMPT "krishna_> "


static bool strings_equal(
    const char *left,
    const char *right
) {
    size_t index = 0;

    while (left[index] != '\0' &&
           right[index] != '\0') {
        if (left[index] != right[index]) {
            return false;
        }

        index++;
    }

    return left[index] == right[index];
}


static bool string_starts_with(
    const char *text,
    const char *prefix
) {
    size_t index = 0;

    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) {
            return false;
        }

        index++;
    }

    return true;
}


static void shell_show_prompt(
    struct shell *shell
) {
    console_write(
        shell->console,
        SHELL_PROMPT
    );
}


static void shell_execute(
    struct shell *shell
) {
    const char *command = shell->input;

    /*
     * Pressing Enter on an empty line simply produces another prompt.
     */
    if (command[0] == '\0') {
        return;
    }

    if (strings_equal(command, "help")) {
        console_write(
            shell->console,
            "Available commands:\n"
            "  help             Show this command list\n"
            "  about            Information about KRISHNA OS\n"
            "  memory           Show usable physical memory\n"
            "  echo <message>   Print a message\n"
            "  clear            Clear the terminal\n"
            "  memtest          Test physical and virtual memory\n"
            "  faulttest        Deliberately test page-fault handling\n"
        );

        return;
    }

    if (strings_equal(command, "about")) {
        console_write(
            shell->console,
            "KRISHNA OS\n"
            "A handcrafted x86-64 operating system.\n"
            "Bootloader: Limine\n"
            "Display: Linear framebuffer\n"
            "Architecture: x86-64\n"
        );

        return;
    }

    if (strings_equal(command, "clear")) {
        console_clear(shell->console);
        return;
    }

    if (strings_equal(command, "memory")) {
        console_write(
            shell->console,
            "Usable physical memory: "
        );

        console_write_u64(
            shell->console,
            shell->usable_memory_bytes >> 20
        );

        console_write(
            shell->console,
            " MiB\n"
        );

        return;
    }

    if (strings_equal(command, "echo")) {
        console_put_character(
            shell->console,
            '\n'
        );

        return;
    }

    if (string_starts_with(command, "echo ")) {
        console_write(
            shell->console,
            command + 5
        );

        console_put_character(
            shell->console,
            '\n'
        );

        return;
    }

    if (strings_equal(command, "memtest")) {
        struct pmm_statistics statistics;
        struct memory_test_result test;

        /*
        * Capture allocator statistics before running the test.
        */
        pmm_get_statistics(&statistics);

        console_write(
            shell->console,
            "KRISHNA memory diagnostics\n"
            "Managed pages: "
        );

        console_write_u64(
            shell->console,
            statistics.managed_pages
        );

        console_write(
            shell->console,
            "\nFree pages before test: "
        );

        console_write_u64(
            shell->console,
            statistics.free_pages
        );

        console_write(shell->console, "\n\n");

        /*
        * This performs real allocations and page-table mappings.
        * Every allocated resource is released before it returns.
        */
        bool passed =
            memory_run_self_test(&test);

        console_write(
            shell->console,
            "Physical allocation: "
        );

        console_write(
            shell->console,
            test.allocation_passed ?
                "PASS\n" : "FAIL\n"
        );

        console_write(
            shell->console,
            "Distinct frames:     "
        );

        console_write(
            shell->console,
            test.distinct_pages_passed ?
                "PASS\n" : "FAIL\n"
        );

        console_write(
            shell->console,
            "Virtual mapping:     "
        );

        console_write(
            shell->console,
            test.mapping_passed ?
                "PASS\n" : "FAIL\n"
        );

        console_write(
            shell->console,
            "Address translation: "
        );

        console_write(
            shell->console,
            test.translation_passed ?
                "PASS\n" : "FAIL\n"
        );

        console_write(
            shell->console,
            "Mapped write/read:   "
        );

        console_write(
            shell->console,
            test.write_read_passed ?
                "PASS\n" : "FAIL\n"
        );

        console_write(
            shell->console,
            "Resource cleanup:    "
        );

        console_write(
            shell->console,
            test.cleanup_passed ?
                "PASS\n" : "FAIL\n"
        );

        console_write(
            shell->console,
            "\nMemory subsystem: "
        );

        console_write(
            shell->console,
            passed ? "HEALTHY\n" : "FAILED\n"
        );

        return;
    }

    if (strings_equal(command, "faulttest")) {
        const uint64_t test_address =
            UINT64_C(0x0000620000000000);

        uint64_t translated_address;

        /*
        * Ensure our chosen test address really is unmapped.
        */
        if (vmm_translate(
                vmm_kernel_address_space(),
                test_address,
                &translated_address
            )) {
            console_write(
                shell->console,
                "Fault test cancelled: test address is mapped.\n"
            );

            return;
        }

        console_write(
            shell->console,
            "Triggering a controlled kernel page fault.\n"
            "The graphical interface will stop.\n"
            "Read the diagnostic report in the serial terminal.\n"
        );

        serial_write(
            "\n[TEST] Triggering controlled page fault\n"
        );

        /*
        * Volatile forces the CPU to perform the memory read.
        */
        volatile uint64_t value =
            *(volatile uint64_t *)(uintptr_t)test_address;

        /*
        * This point must never be reached.
        */
        (void)value;

        console_write(
            shell->console,
            "FAIL: page fault was not generated.\n"
        );

        return;
    }

    console_write(
        shell->console,
        "Unknown command: "
    );

    console_write(
        shell->console,
        command
    );

    console_write(
        shell->console,
        "\nType 'help' for available commands.\n"
    );
}


void shell_init(
    struct shell *shell,
    struct console *console,
    uint64_t usable_memory_bytes
) {
    shell->console = console;
    shell->input_length = 0;
    shell->input[0] = '\0';

    shell->usable_memory_bytes =
        usable_memory_bytes;

    shell_show_prompt(shell);
}


void shell_handle_character(
    struct shell *shell,
    char character
) {
    /*
     * Execute the completed command.
     */
    if (character == '\n') {
        console_put_character(
            shell->console,
            '\n'
        );

        shell->input[shell->input_length] =
            '\0';

        shell_execute(shell);

        shell->input_length = 0;
        shell->input[0] = '\0';

        shell_show_prompt(shell);
        return;
    }

    /*
     * Remove one character from both the input buffer
     * and framebuffer.
     */
    if (character == '\b') {
        if (shell->input_length == 0) {
            return;
        }

        shell->input_length--;

        shell->input[shell->input_length] =
            '\0';

        console_put_character(
            shell->console,
            '\b'
        );

        return;
    }

    /*
     * Ignore control characters.
     */
    if (character < 32 ||
        character > 126) {
        return;
    }

    /*
     * Leave one byte available for the terminating null character.
     */
    if (shell->input_length >=
        SHELL_INPUT_CAPACITY - 1) {
        return;
    }

    shell->input[shell->input_length] =
        character;

    shell->input_length++;

    shell->input[shell->input_length] =
        '\0';

    console_put_character(
        shell->console,
        character
    );
}