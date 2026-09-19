#ifndef KRISHNA_TERMINAL_PROTOCOL_H
#define KRISHNA_TERMINAL_PROTOCOL_H

#include <stdint.h>

#include <krishna/abi.h>

#define TERMINAL_PROTOCOL_TEXT_CAPACITY 120
#define TERMINAL_PROTOCOL_LINE_CAPACITY 80

enum terminal_message_type {
    TERMINAL_MESSAGE_PING = 1,
    TERMINAL_MESSAGE_PONG,
    TERMINAL_MESSAGE_KEY,
    TERMINAL_MESSAGE_STATE
};

struct terminal_message {
    uint32_t type;
    uint32_t length;

    union {
        struct krishna_keyboard_event key;
        char text[TERMINAL_PROTOCOL_TEXT_CAPACITY];
    } payload;
};

_Static_assert(
    sizeof(struct terminal_message) <=
        KRISHNA_CHANNEL_MAX_MESSAGE_SIZE,
    "Terminal message exceeds channel message size"
);

#endif