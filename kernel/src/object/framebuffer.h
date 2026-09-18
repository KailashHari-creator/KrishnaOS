#ifndef KRISHNA_OBJECT_FRAMEBUFFER_H
#define KRISHNA_OBJECT_FRAMEBUFFER_H

#include <stdbool.h>

#include <limine.h>

struct kernel_process;

bool framebuffer_object_init(
    struct limine_framebuffer *framebuffer
);

bool framebuffer_object_attach(
    struct kernel_process *process
);

#endif