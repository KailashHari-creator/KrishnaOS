#include "object/channel.h"

#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>

#include "memory/heap.h"
#include "object/object.h"
#include "sync/spinlock.h"

#define CHANNEL_QUEUE_CAPACITY ((size_t)16)

struct channel_message {
    size_t size;
    uint8_t data[KRISHNA_CHANNEL_MAX_MESSAGE_SIZE];
};

struct channel_queue {
    struct channel_message messages[CHANNEL_QUEUE_CAPACITY];
    size_t read_index;
    size_t write_index;
    size_t count;
};

struct kernel_channel;

struct channel_endpoint {
    struct kernel_channel *channel;
    size_t side;
};

struct kernel_channel {
    spinlock_t lock;

    bool endpoint_open[2];

    struct channel_queue incoming[2];

    struct kernel_object objects[2];
    struct channel_endpoint endpoints[2];
};

static int64_t channel_read(
    void *context,
    void *buffer,
    size_t size
)
{
    struct channel_endpoint *endpoint = context;

    if (endpoint == NULL ||
        endpoint->channel == NULL ||
        buffer == NULL) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    struct kernel_channel *channel =
        endpoint->channel;

    interrupt_state_t state =
        spinlock_lock_irqsave(&channel->lock);

    struct channel_queue *queue =
        &channel->incoming[endpoint->side];

    if (queue->count == 0) {
        spinlock_unlock_irqrestore(
            &channel->lock,
            state
        );

        return -KRISHNA_ERROR_WOULD_BLOCK;
    }

    struct channel_message *message =
        &queue->messages[queue->read_index];

    if (size < message->size) {
        spinlock_unlock_irqrestore(
            &channel->lock,
            state
        );

        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    for (size_t index = 0;
         index < message->size;
         index++) {
        ((uint8_t *)buffer)[index] =
            message->data[index];
    }

    size_t result = message->size;

    queue->read_index =
        (queue->read_index + 1) %
        CHANNEL_QUEUE_CAPACITY;

    queue->count--;

    spinlock_unlock_irqrestore(
        &channel->lock,
        state
    );

    return (int64_t)result;
}

static int64_t channel_write(
    void *context,
    const void *buffer,
    size_t size
)
{
    struct channel_endpoint *endpoint = context;

    if (endpoint == NULL ||
        endpoint->channel == NULL ||
        buffer == NULL ||
        size == 0 ||
        size > KRISHNA_CHANNEL_MAX_MESSAGE_SIZE) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    struct kernel_channel *channel =
        endpoint->channel;

    size_t peer_side =
        endpoint->side ^ (size_t)1;

    interrupt_state_t state =
        spinlock_lock_irqsave(&channel->lock);

    if (!channel->endpoint_open[peer_side]) {
        spinlock_unlock_irqrestore(
            &channel->lock,
            state
        );

        return -KRISHNA_ERROR_IO;
    }

    struct channel_queue *queue =
        &channel->incoming[peer_side];

    if (queue->count == CHANNEL_QUEUE_CAPACITY) {
        spinlock_unlock_irqrestore(
            &channel->lock,
            state
        );

        return -KRISHNA_ERROR_WOULD_BLOCK;
    }

    struct channel_message *message =
        &queue->messages[queue->write_index];

    message->size = size;

    for (size_t index = 0;
         index < size;
         index++) {
        message->data[index] =
            ((const uint8_t *)buffer)[index];
    }

    queue->write_index =
        (queue->write_index + 1) %
        CHANNEL_QUEUE_CAPACITY;

    queue->count++;

    spinlock_unlock_irqrestore(
        &channel->lock,
        state
    );

    return (int64_t)size;
}

static void channel_destroy(void *context)
{
    struct channel_endpoint *endpoint = context;

    if (endpoint == NULL ||
        endpoint->channel == NULL) {
        return;
    }

    struct kernel_channel *channel =
        endpoint->channel;

    interrupt_state_t state =
        spinlock_lock_irqsave(&channel->lock);

    channel->endpoint_open[endpoint->side] =
        false;

    bool release_channel =
        !channel->endpoint_open[0] &&
        !channel->endpoint_open[1];

    spinlock_unlock_irqrestore(
        &channel->lock,
        state
    );

    if (release_channel) {
        (void)kfree(channel);
    }
}

static const struct kernel_object_operations
channel_operations = {
    .read = channel_read,
    .write = channel_write,
    .ioctl = NULL,
    .map = NULL,
    .unmap = NULL,
    .destroy = channel_destroy
};

bool kernel_channel_create_pair(
    struct kernel_object **first,
    struct kernel_object **second
)
{
    if (first == NULL || second == NULL) {
        return false;
    }

    struct kernel_channel *channel =
        kcalloc(1, sizeof(struct kernel_channel));

    if (channel == NULL) {
        return false;
    }

    spinlock_init(&channel->lock);

    channel->endpoint_open[0] = true;
    channel->endpoint_open[1] = true;

    channel->endpoints[0].channel = channel;
    channel->endpoints[0].side = 0;

    channel->endpoints[1].channel = channel;
    channel->endpoints[1].side = 1;

    if (!kernel_object_initialize(
            &channel->objects[0],
            &channel_operations,
            &channel->endpoints[0]
        ) ||
        !kernel_object_initialize(
            &channel->objects[1],
            &channel_operations,
            &channel->endpoints[1]
        )) {
        (void)kfree(channel);
        return false;
    }

    *first = &channel->objects[0];
    *second = &channel->objects[1];

    return true;
}