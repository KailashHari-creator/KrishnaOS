#include "object/object.h"

#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>

bool kernel_object_initialize(
    struct kernel_object *object,
    const struct kernel_object_operations *operations,
    void *context
)
{
    if (object == NULL ||
        operations == NULL) {
        return false;
    }

    object->operations = operations;
    object->context = context;

    __atomic_store_n(
        &object->reference_count,
        UINT64_C(1),
        __ATOMIC_RELEASE
    );

    return true;
}

bool kernel_object_retain(
    struct kernel_object *object
)
{
    if (object == NULL) {
        return false;
    }

    uint64_t references =
        __atomic_load_n(
            &object->reference_count,
            __ATOMIC_ACQUIRE
        );

    for (;;) {
        if (references == 0 ||
            references == UINT64_MAX) {
            return false;
        }

        if (__atomic_compare_exchange_n(
                &object->reference_count,
                &references,
                references + UINT64_C(1),
                false,
                __ATOMIC_ACQ_REL,
                __ATOMIC_ACQUIRE
            )) {
            return true;
        }
    }
}

void kernel_object_release(
    struct kernel_object *object
)
{
    if (object == NULL) {
        return;
    }

    uint64_t references =
        __atomic_load_n(
            &object->reference_count,
            __ATOMIC_ACQUIRE
        );

    for (;;) {
        if (references == 0) {
            return;
        }

        if (__atomic_compare_exchange_n(
                &object->reference_count,
                &references,
                references - UINT64_C(1),
                false,
                __ATOMIC_ACQ_REL,
                __ATOMIC_ACQUIRE
            )) {
            break;
        }
    }

    if (references == UINT64_C(1) &&
        object->operations != NULL &&
        object->operations->destroy != NULL) {
        object->operations->destroy(
            object->context
        );
    }
}

int64_t kernel_object_read(
    struct kernel_object *object,
    void *buffer,
    size_t size
)
{
    if (object == NULL) {
        return -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR;
    }

    if (size != 0 && buffer == NULL) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    if (object->operations == NULL ||
        object->operations->read == NULL) {
        return -KRISHNA_ERROR_NOT_IMPLEMENTED;
    }

    return object->operations->read(
        object->context,
        buffer,
        size
    );
}

int64_t kernel_object_write(
    struct kernel_object *object,
    const void *buffer,
    size_t size
)
{
    if (object == NULL) {
        return -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR;
    }

    if (size != 0 && buffer == NULL) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    if (object->operations == NULL ||
        object->operations->write == NULL) {
        return -KRISHNA_ERROR_NOT_IMPLEMENTED;
    }

    return object->operations->write(
        object->context,
        buffer,
        size
    );
}

int64_t kernel_object_ioctl(
    struct kernel_object *object,
    uint64_t request,
    void *buffer,
    size_t size
)
{
    if (object == NULL) {
        return -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR;
    }

    if (size != 0 && buffer == NULL) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    if (object->operations == NULL ||
        object->operations->ioctl == NULL) {
        return -KRISHNA_ERROR_NOT_IMPLEMENTED;
    }

    return object->operations->ioctl(
        object->context,
        request,
        buffer,
        size
    );
}

int64_t kernel_object_map(
    struct kernel_object *object,
    struct kernel_process *process,
    uint64_t requested_address,
    uint64_t offset,
    uint64_t length,
    uint64_t protection,
    uint64_t flags
)
{
    if (object == NULL) {
        return -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR;
    }

    if (process == NULL ||
        length == 0) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    if (object->operations == NULL ||
        object->operations->map == NULL) {
        return -KRISHNA_ERROR_NOT_IMPLEMENTED;
    }

    return object->operations->map(
        object->context,
        process,
        requested_address,
        offset,
        length,
        protection,
        flags
    );
}

int64_t kernel_object_unmap(
    struct kernel_object *object,
    struct kernel_process *process,
    uint64_t address,
    uint64_t length
)
{
    if (object == NULL) {
        return -KRISHNA_ERROR_BAD_FILE_DESCRIPTOR;
    }

    if (process == NULL ||
        length == 0) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    if (object->operations == NULL ||
        object->operations->unmap == NULL) {
        return -KRISHNA_ERROR_NOT_IMPLEMENTED;
    }

    return object->operations->unmap(
        object->context,
        process,
        address,
        length
    );
}