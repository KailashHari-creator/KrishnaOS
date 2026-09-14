#include "shell.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/memory_test.h"
#include "memory/pmm.h"

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
            "  memtest          Test physical and virtual memory\n"
            "  echo <message>   Print a message\n"
            "  clear            Clear the terminal\n"
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

    if (strings_equal(command, "memtest")) {
        struct pmm_statistics statistics;
        struct memory_test_result test;

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
        console_write(shell->console, "\nFree pages: ");
        console_write_u64(
            shell->console,
            statistics.free_pages
        );
        console_write(shell->console, "\n");

        bool passed = memory_run_self_test(&test);

        console_write(shell->console, "Allocation:  ");
        console_write(
            shell->console,
            test.allocation_passed ? "PASS\n" : "FAIL\n"
        );
        console_write(shell->console, "Distinct:    ");
        console_write(
            shell->console,
            test.distinct_pages_passed ? "PASS\n" : "FAIL\n"
        );
        console_write(shell->console, "Mapping:     ");
        console_write(
            shell->console,
            test.mapping_passed ? "PASS\n" : "FAIL\n"
        );
        console_write(shell->console, "Translation: ");
        console_write(
            shell->console,
            test.translation_passed ? "PASS\n" : "FAIL\n"
        );
        console_write(shell->console, "Write/read:  ");
        console_write(
            shell->console,
            test.write_read_passed ? "PASS\n" : "FAIL\n"
        );
        console_write(shell->console, "Cleanup:     ");
        console_write(
            shell->console,
            test.cleanup_passed ? "PASS\n" : "FAIL\n"
        );
        console_write(
            shell->console,
            passed ?
                "Memory subsystem: HEALTHY\n" :
                "Memory subsystem: FAILED\n"
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
