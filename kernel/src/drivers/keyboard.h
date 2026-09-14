#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

struct key_event {
    uint8_t scancode;
    char character;
    bool pressed;
};

bool keyboard_poll(struct key_event *event);

#endif