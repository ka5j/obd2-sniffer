#include "frame_queue.h"

#define FRAME_SIZE ((uint32_t)sizeof(can_frame_t))

bool frame_queue_init(frame_queue_t *q, uint8_t *storage, uint32_t capacity)
{
    if ((q == NULL) || (capacity < FRAME_SIZE)) {
        return false;
    }
    return ring_init(&q->ring, storage, capacity);
}

bool frame_queue_push(frame_queue_t *q, const can_frame_t *frame)
{
    /* Only the producer adds data, so the free space can only grow between
     * this check and the write: the whole frame is guaranteed to fit. */
    if (ring_free(&q->ring) < FRAME_SIZE) {
        return false;
    }
    /* Viewing an object as bytes is the pointer conversion MISRA allows
     * (Rule 11.3 exception for character types). */
    (void)ring_write(&q->ring, (const uint8_t *)frame, FRAME_SIZE);
    return true;
}

bool frame_queue_pop(frame_queue_t *q, can_frame_t *frame)
{
    /* Frames are published whole, so a full frame's worth of data is a
     * complete frame. */
    if (ring_used(&q->ring) < FRAME_SIZE) {
        return false;
    }
    (void)ring_read(&q->ring, (uint8_t *)frame, FRAME_SIZE);
    return true;
}

uint32_t frame_queue_count(const frame_queue_t *q)
{
    return ring_used(&q->ring) / FRAME_SIZE;
}

uint32_t frame_queue_capacity(const frame_queue_t *q)
{
    return ring_capacity(&q->ring) / FRAME_SIZE;
}
