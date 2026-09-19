#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>
#include <krishna/io.h>
#include <krishna/ipc.h>
#include <krishna/process.h>
#include <krishna/terminal_protocol.h>

static char input_line[
    TERMINAL_PROTOCOL_LINE_CAPACITY
];

static size_t input_length;

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

static void send_message(
    const struct terminal_message *message,
    size_t size
)
{
    for (;;) {
        int64_t result =
            krishna_channel_send(
                KRISHNA_HANDLE_APPLICATION_CHANNEL,
                message,
                size
            );

        if (result == (int64_t)size) {
            return;
        }

        if (result !=
            -KRISHNA_ERROR_WOULD_BLOCK) {
            return;
        }

        (void)krishna_yield();
    }
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

    message.payload.text[input_length] = '\0';

    send_message(
        &message,
        offsetof(
            struct terminal_message,
            payload.text
        ) +
        input_length +
        1
    );
}

static void handle_key(
    const struct krishna_keyboard_event *key
)
{
    if (key->pressed == 0) {
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
        static const char submitted[] =
            "[TERMINAL] command submitted through IPC\n";

        serial_message(
            submitted,
            sizeof(submitted) - 1
        );

        input_length = 0;
        input_line[0] = '\0';
        send_state();
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

    for (;;) {
        struct terminal_message message;

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
            static const char failed[] =
                "[TERMINAL] IPC receive failed\n";

            serial_message(
                failed,
                sizeof(failed) - 1
            );

            (void)krishna_exit(1);
        }

        if (message.type ==
            TERMINAL_MESSAGE_PING) {
            struct terminal_message reply = {
                .type = TERMINAL_MESSAGE_PONG,
                .length = 0
            };

            send_message(
                &reply,
                offsetof(
                    struct terminal_message,
                    payload
                )
            );

            send_state();
        } else if (
            message.type ==
                TERMINAL_MESSAGE_KEY
        ) {
            handle_key(&message.payload.key);
        }
    }
}