#ifndef KRISHNA_VENU_PROTOCOL_H
#define KRISHNA_VENU_PROTOCOL_H

#include <stdint.h>

#include <krishna/abi.h>

#define VENU_PROTOCOL_TEXT_CAPACITY 120

enum venu_message_type {
    VENU_MESSAGE_PING = 1,
    VENU_MESSAGE_PONG,
    VENU_MESSAGE_LINE,
    VENU_MESSAGE_OUTPUT,
    VENU_MESSAGE_CLEAR,
    VENU_MESSAGE_COMPLETE
};

struct venu_message {
    uint32_t type;

    /*
     * LINE/OUTPUT: number of bytes in text.
     * COMPLETE: command exit status.
     */
    uint32_t length;

    char text[VENU_PROTOCOL_TEXT_CAPACITY];
};

_Static_assert(
    sizeof(struct venu_message) <=
        KRISHNA_CHANNEL_MAX_MESSAGE_SIZE,
    "VENU message exceeds channel capacity"
);

#endif