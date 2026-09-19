#ifndef KRISHNA_OBJECT_CHANNEL_H
#define KRISHNA_OBJECT_CHANNEL_H

#include <stdbool.h>

struct kernel_object;

bool kernel_channel_create_pair(
    struct kernel_object **first,
    struct kernel_object **second
);

#endif