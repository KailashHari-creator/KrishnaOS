#ifndef KRISHNA_OBJECT_OBJECT_H
#define KRISHNA_OBJECT_OBJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct kernel_object;
struct kernel_process;

struct kernel_object_operations {
    int64_t (*read)(
        void *context,
        void *buffer,
        size_t size
    );

    int64_t (*write)(
        void *context,
        const void *buffer,
        size_t size
    );

        int64_t (*ioctl)(
        void *context,
        uint64_t request,
        void *buffer,
        size_t size
    );

    int64_t (*map)(
        void *context,
        struct kernel_process *process,
        uint64_t requested_address,
        uint64_t offset,
        uint64_t length,
        uint64_t protection,
        uint64_t flags
    );

    int64_t (*unmap)(
        void *context,
        struct kernel_process *process,
        uint64_t address,
        uint64_t length
    );

    void (*destroy)(
        void *context
    );
};

struct kernel_object {
    const struct kernel_object_operations *operations;
    void *context;
    uint64_t reference_count;
};

bool kernel_object_initialize(
    struct kernel_object *object,
    const struct kernel_object_operations *operations,
    void *context
);

bool kernel_object_retain(
    struct kernel_object *object
);

void kernel_object_release(
    struct kernel_object *object
);

int64_t kernel_object_read(
    struct kernel_object *object,
    void *buffer,
    size_t size
);

int64_t kernel_object_write(
    struct kernel_object *object,
    const void *buffer,
    size_t size
);

int64_t kernel_object_ioctl(
    struct kernel_object *object,
    uint64_t request,
    void *buffer,
    size_t size
);

int64_t kernel_object_map(
    struct kernel_object *object,
    struct kernel_process *process,
    uint64_t requested_address,
    uint64_t offset,
    uint64_t length,
    uint64_t protection,
    uint64_t flags
);

int64_t kernel_object_unmap(
    struct kernel_object *object,
    struct kernel_process *process,
    uint64_t address,
    uint64_t length
);

#endif