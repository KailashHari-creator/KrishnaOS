#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>
#include <krishna/io.h>
#include <krishna/ipc.h>
#include <krishna/process.h>
#include <krishna/venu_protocol.h>

#include "lexer.h"

static bool text_equal(
    const char *left,
    const char *right
)
{
    if (left == NULL || right == NULL) {
        return false;
    }

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

static size_t text_length(const char *text)
{
    size_t length = 0;

    if (text == NULL) {
        return 0;
    }

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static void write_text(const char *text)
{
    (void)krishna_write(
        KRISHNA_STDOUT,
        text,
        text_length(text)
    );
}

static void write_line(const char *text)
{
    write_text(text);
    write_text("\n");
}

static size_t unsigned_to_text(
    uint64_t value,
    char *buffer,
    size_t capacity
)
{
    if (buffer == NULL || capacity < 2) {
        return 0;
    }

    char reversed[32];
    size_t count = 0;

    do {
        reversed[count++] =
            (char)('0' + value % 10);

        value /= 10;
    } while (value != 0 &&
             count < sizeof(reversed));

    if (count + 1 > capacity) {
        return 0;
    }

    for (size_t index = 0;
         index < count;
         index++) {
        buffer[index] =
            reversed[count - index - 1];
    }

    buffer[count] = '\0';
    return count;
}

static bool parse_unsigned(
    const char *text,
    uint64_t *result
)
{
    if (text == NULL ||
        result == NULL ||
        text[0] == '\0') {
        return false;
    }

    uint64_t value = 0;

    for (size_t index = 0;
         text[index] != '\0';
         index++) {
        char character = text[index];

        if (character < '0' ||
            character > '9') {
            return false;
        }

        uint64_t digit =
            (uint64_t)(character - '0');

        if (value >
            (UINT64_MAX - digit) / 10) {
            return false;
        }

        value =
            value * 10 +
            digit;
    }

    *result = value;
    return true;
}

static int command_help(
    const struct venu_token_list *tokens
)
{
    if (tokens->count == 2) {
        const char *name =
            tokens->tokens[1].text;

        if (text_equal(name, "echo")) {
            write_line(
                "usage: echo [arguments...]"
            );

            write_line(
                "Print arguments separated by spaces."
            );

            return 0;
        }

        if (text_equal(name, "sleep")) {
            write_line(
                "usage: sleep <milliseconds>"
            );

            write_line(
                "Pause VENU for the requested duration."
            );

            return 0;
        }

        write_text(
            "venu: help: unknown command: "
        );

        write_line(name);
        return 1;
    }

    if (tokens->count > 2) {
        write_line(
            "usage: help [command]"
        );

        return 2;
    }

    write_line(
        "VENU - Versatile Execution and Navigation Utility"
    );

    write_line(
        "Available commands:"
    );

    write_line(
        "  help [command]       Show command help"
    );

    write_line(
        "  echo [arguments...]   Print arguments"
    );

    write_line(
        "  about                 About KRISHNA OS"
    );

    write_line(
        "  version               Show VENU version"
    );

    write_line(
        "  pid                   Show shell process ID"
    );

    write_line(
        "  sleep <milliseconds>  Pause the shell"
    );

    write_line(
        "  true                  Return success"
    );

    write_line(
        "  false                 Return failure"
    );

    return 0;
}

static int command_echo(
    const struct venu_token_list *tokens
)
{
    for (size_t index = 1;
         index < tokens->count;
         index++) {
        if (index != 1) {
            write_text(" ");
        }

        write_text(
            tokens->tokens[index].text
        );
    }

    write_text("\n");
    return 0;
}

static int command_about(
    const struct venu_token_list *tokens
)
{
    if (tokens->count != 1) {
        write_line("usage: about");
        return 2;
    }

    write_line(
        "KRISHNA OS is a from-scratch x86-64 operating system."
    );

    write_line(
        "VENU is its native user-space command language."
    );

    return 0;
}

static int command_version(
    const struct venu_token_list *tokens
)
{
    if (tokens->count != 1) {
        write_line("usage: version");
        return 2;
    }

    write_line("VENU 0.1.0");
    write_line("KRISHNA userspace ABI 0");

    return 0;
}

static int command_pid(
    const struct venu_token_list *tokens
)
{
    if (tokens->count != 1) {
        write_line("usage: pid");
        return 2;
    }

    int64_t process_id =
        krishna_getpid();

    if (process_id < 0) {
        write_line(
            "venu: pid: getpid failed"
        );

        return 1;
    }

    char number[32];

    if (unsigned_to_text(
            (uint64_t)process_id,
            number,
            sizeof(number)
        ) == 0) {
        write_line(
            "venu: pid: conversion failed"
        );

        return 1;
    }

    write_text("VENU process ID: ");
    write_line(number);

    return 0;
}

static int command_sleep(
    const struct venu_token_list *tokens
)
{
    if (tokens->count != 2) {
        write_line(
            "usage: sleep <milliseconds>"
        );

        return 2;
    }

    uint64_t milliseconds;

    if (!parse_unsigned(
            tokens->tokens[1].text,
            &milliseconds
        )) {
        write_line(
            "venu: sleep: invalid duration"
        );

        return 2;
    }

    int64_t result =
        krishna_sleep(milliseconds);

    if (result < 0) {
        write_line(
            "venu: sleep: syscall failed"
        );

        return 1;
    }

    return 0;
}

static int execute_line(
    const char *line,
    size_t length
)
{
    struct venu_token_list tokens;

    enum venu_lexer_result lex_result =
        venu_lex(
            line,
            length,
            &tokens
        );

    if (lex_result != VENU_LEXER_OK) {
        switch (lex_result) {
            case VENU_LEXER_TOO_MANY_TOKENS:
                write_line(
                    "venu: too many tokens"
                );
                break;

            case VENU_LEXER_TOKEN_TOO_LONG:
                write_line(
                    "venu: token is too long"
                );
                break;

            case VENU_LEXER_UNTERMINATED_QUOTE:
                write_line(
                    "venu: unterminated quote"
                );
                break;

            case VENU_LEXER_TRAILING_ESCAPE:
                write_line(
                    "venu: trailing escape"
                );
                break;

            default:
                write_line(
                    "venu: lexer failure"
                );
                break;
        }

        return 2;
    }

    if (tokens.count == 0) {
        return 0;
    }

    const char *command =
        tokens.tokens[0].text;

    if (text_equal(command, "help")) {
        return command_help(&tokens);
    }

    if (text_equal(command, "echo")) {
        return command_echo(&tokens);
    }

    if (text_equal(command, "about")) {
        return command_about(&tokens);
    }

    if (text_equal(command, "version")) {
        return command_version(&tokens);
    }

    if (text_equal(command, "pid")) {
        return command_pid(&tokens);
    }

    if (text_equal(command, "sleep")) {
        return command_sleep(&tokens);
    }

    if (text_equal(command, "true")) {
        return tokens.count == 1 ? 0 : 2;
    }

    if (text_equal(command, "false")) {
        return tokens.count == 1 ? 1 : 2;
    }

    write_text("venu: command not found: ");
    write_line(command);

    return 127;
}

static void send_pong(void)
{
    struct venu_message message = {
        .type = VENU_MESSAGE_PONG,
        .length = 0,
        .text = {0}
    };

    for (;;) {
        int64_t result =
            krishna_channel_send(
                KRISHNA_HANDLE_APPLICATION_CHANNEL,
                &message,
                offsetof(
                    struct venu_message,
                    text
                )
            );

        if (result >= 0) {
            return;
        }

        if (result !=
            -KRISHNA_ERROR_WOULD_BLOCK) {
            return;
        }

        (void)krishna_yield();
    }
}

int main(void)
{
    write_line(
        "[VENU] shell process started"
    );

    for (;;) {
        struct venu_message message;

        int64_t result =
            krishna_channel_receive(
                KRISHNA_HANDLE_APPLICATION_CHANNEL,
                &message,
                sizeof(message)
            );

        if (result ==
            -KRISHNA_ERROR_WOULD_BLOCK) {
            (void)krishna_yield();
            continue;
        }

        if (result < 0) {
            write_line(
                "[VENU] shell channel failed"
            );

            krishna_exit(1);
        }

        if (message.type ==
            VENU_MESSAGE_PING) {
            send_pong();
            continue;
        }

        if (message.type ==
            VENU_MESSAGE_LINE) {
            if (message.length >=
                VENU_PROTOCOL_TEXT_CAPACITY) {
                write_line(
                    "venu: submitted line is too long"
                );

                continue;
            }

            message.text[message.length] =
                '\0';

            (void)execute_line(
                message.text,
                message.length
            );
        }
    }
}