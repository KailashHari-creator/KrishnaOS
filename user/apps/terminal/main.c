#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>
#include <krishna/io.h>
#include <krishna/ipc.h>
#include <krishna/process.h>
#include <krishna/terminal_protocol.h>
#include <krishna/venu_protocol.h>

static const char prompt[] =
    "kailash@krishna:~$ ";

static char input_line[
    TERMINAL_PROTOCOL_LINE_CAPACITY
];

static size_t input_length;

static uint64_t shell_channel =
    KRISHNA_HANDLE_INVALID;

static bool shell_ready;

static void serial_message(
    const char *text,
    size_t size
)
{
    (void)krishna_write(
        KRISHNA_STDOUT,
        text,
        size
    );
}

static size_t local_text_length(
    const char *text
)
{
    size_t length = 0;

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static bool send_channel(
    uint64_t channel,
    const void *message,
    size_t size
)
{
    for (;;) {
        int64_t result =
            krishna_channel_send(
                channel,
                message,
                size
            );

        if (result == (int64_t)size) {
            return true;
        }

        if (result !=
            -KRISHNA_ERROR_WOULD_BLOCK) {
            return false;
        }

        (void)krishna_yield();
    }
}

static bool send_desktop_message(
    const struct terminal_message *message,
    size_t size
)
{
    return send_channel(
        KRISHNA_HANDLE_APPLICATION_CHANNEL,
        message,
        size
    );
}

static void send_state(void)
{
    struct terminal_message message = {
        .type = TERMINAL_MESSAGE_STATE,
        .length = (uint32_t)input_length
    };

    for (size_t index = 0;
         index < input_length;
         index++) {
        message.payload.text[index] =
            input_line[index];
    }

    message.payload.text[input_length] =
        '\0';

    (void)send_desktop_message(
        &message,
        offsetof(
            struct terminal_message,
            payload.text
        ) +
        input_length +
        1
    );
}

static void send_ready(void)
{
    struct terminal_message message = {
        .type = TERMINAL_MESSAGE_READY,
        .length = shell_ready ? 1 : 0
    };

    (void)send_desktop_message(
        &message,
        offsetof(
            struct terminal_message,
            payload
        )
    );
}

static void send_clear(void)
{
    struct terminal_message message = {
        .type = TERMINAL_MESSAGE_CLEAR,
        .length = 0
    };

    (void)send_desktop_message(
        &message,
        offsetof(
            struct terminal_message,
            payload
        )
    );
}

static void send_output(
    const char *text,
    size_t length
)
{
    size_t position = 0;

    while (position < length) {
        size_t chunk = length - position;

        if (chunk >
            TERMINAL_PROTOCOL_TEXT_CAPACITY) {
            chunk =
                TERMINAL_PROTOCOL_TEXT_CAPACITY;
        }

        struct terminal_message message = {
            .type = TERMINAL_MESSAGE_OUTPUT,
            .length = (uint32_t)chunk
        };

        for (size_t index = 0;
             index < chunk;
             index++) {
            message.payload.text[index] =
                text[position + index];
        }

        if (!send_desktop_message(
                &message,
                offsetof(
                    struct terminal_message,
                    payload.text
                ) +
                chunk
            )) {
            return;
        }

        position += chunk;
    }
}

static void echo_submitted_line(void)
{
    char line[
        TERMINAL_PROTOCOL_TEXT_CAPACITY
    ];

    size_t position = 0;
    size_t prompt_length =
        local_text_length(prompt);

    for (size_t index = 0;
         index < prompt_length;
         index++) {
        line[position++] = prompt[index];
    }

    for (size_t index = 0;
         index < input_length;
         index++) {
        line[position++] = input_line[index];
    }

    line[position++] = '\n';

    send_output(line, position);
}

static bool submit_line(void)
{
    struct venu_message message = {
        .type = VENU_MESSAGE_LINE,
        .length = (uint32_t)input_length,
        .text = {0}
    };

    for (size_t index = 0;
         index < input_length;
         index++) {
        message.text[index] =
            input_line[index];
    }

    return send_channel(
        shell_channel,
        &message,
        offsetof(
            struct venu_message,
            text
        ) +
        input_length
    );
}

static void handle_key(
    const struct krishna_keyboard_event *key
)
{
    if (key->pressed == 0 ||
        !shell_ready) {
        return;
    }

    uint8_t character = key->character;

    if (character == '\b') {
        if (input_length != 0) {
            input_length--;
            input_line[input_length] = '\0';
            send_state();
        }

        return;
    }

    if (character == '\n') {
        echo_submitted_line();

        shell_ready = false;
        send_ready();

        bool submitted = submit_line();

        input_length = 0;
        input_line[0] = '\0';
        send_state();

        if (!submitted) {
            static const char error[] =
                "terminal: failed to submit command\n";

            send_output(
                error,
                sizeof(error) - 1
            );

            shell_ready = true;
            send_ready();
        }

        return;
    }

    if (character < 32 ||
        character > 126 ||
        input_length >=
            TERMINAL_PROTOCOL_LINE_CAPACITY - 1) {
        return;
    }

    input_line[input_length++] =
        (char)character;

    input_line[input_length] = '\0';
    send_state();
}

static bool start_shell(void)
{
    struct krishna_channel_pair pair;

    if (krishna_channel_create(&pair) < 0) {
        return false;
    }

    static const char shell_path[] =
        "/system/bin/venu";

    int64_t process_id =
        krishna_process_spawn(
            shell_path,
            sizeof(shell_path) - 1,
            pair.second,
            KRISHNA_HANDLE_APPLICATION_CHANNEL
        );

    if (process_id < 0) {
        (void)krishna_close(pair.first);
        (void)krishna_close(pair.second);
        return false;
    }

    shell_channel = pair.first;
    (void)krishna_close(pair.second);

    struct venu_message ping = {
        .type = VENU_MESSAGE_PING,
        .length = 0,
        .text = {0}
    };

    return send_channel(
        shell_channel,
        &ping,
        offsetof(
            struct venu_message,
            text
        )
    );
}

static bool handle_desktop_message(void)
{
    struct terminal_message message;

    int64_t result =
        krishna_channel_receive(
            KRISHNA_HANDLE_APPLICATION_CHANNEL,
            &message,
            sizeof(message)
        );

    if (result ==
        -KRISHNA_ERROR_WOULD_BLOCK) {
        return false;
    }

    if (result < 0) {
        krishna_exit(1);
    }

    if (message.type ==
        TERMINAL_MESSAGE_PING) {
        struct terminal_message reply = {
            .type = TERMINAL_MESSAGE_PONG,
            .length = 0
        };

        (void)send_desktop_message(
            &reply,
            offsetof(
                struct terminal_message,
                payload
            )
        );

        send_state();
        send_ready();
    } else if (
        message.type ==
            TERMINAL_MESSAGE_KEY
    ) {
        handle_key(
            &message.payload.key
        );
    }

    return true;
}

static bool handle_shell_message(void)
{
    struct venu_message message;

    int64_t result =
        krishna_channel_receive(
            shell_channel,
            &message,
            sizeof(message)
        );

    if (result ==
        -KRISHNA_ERROR_WOULD_BLOCK) {
        return false;
    }

    if (result < 0) {
        static const char failed[] =
            "[TERMINAL] VENU channel failed\n";

        serial_message(
            failed,
            sizeof(failed) - 1
        );

        krishna_exit(1);
    }

    size_t header_size =
        offsetof(
            struct venu_message,
            text
        );

    if ((size_t)result < header_size) {
        return true;
    }

    if (message.type ==
        VENU_MESSAGE_PONG) {
        static const char verified[] =
            "[OK] VENU process and channel verified\n";

        serial_message(
            verified,
            sizeof(verified) - 1
        );

        return true;
    }

    if (message.type ==
        VENU_MESSAGE_CLEAR) {
        send_clear();
        return true;
    }

    if (message.type ==
        VENU_MESSAGE_COMPLETE) {
        shell_ready = true;
        send_ready();
        return true;
    }

    if (message.type ==
        VENU_MESSAGE_OUTPUT) {
        if (message.length >
                VENU_PROTOCOL_TEXT_CAPACITY ||
            (size_t)result <
                header_size +
                message.length) {
            return true;
        }

        send_output(
            message.text,
            message.length
        );
    }

    return true;
}

int main(void)
{
    static const char started[] =
        "[TERMINAL] separate Ring-3 process started\n";

    serial_message(
        started,
        sizeof(started) - 1
    );

    input_length = 0;
    input_line[0] = '\0';
    shell_ready = false;

    if (!start_shell()) {
        static const char failure[] =
            "[TERMINAL] failed to start VENU\n";

        serial_message(
            failure,
            sizeof(failure) - 1
        );

        krishna_exit(1);
    }

    for (;;) {
        bool handled = false;

        if (handle_desktop_message()) {
            handled = true;
        }

        if (handle_shell_message()) {
            handled = true;
        }

        if (!handled) {
            (void)krishna_yield();
        }
    }
}