#ifndef MOUSE_H
#define MOUSE_H

#include <stdbool.h>
#include <stdint.h>

struct mouse_event {
    int16_t delta_x;
    int16_t delta_y;

    bool left_button;
    bool right_button;
    bool middle_button;
};

bool mouse_init(void);
bool mouse_poll(struct mouse_event *event);

#endif