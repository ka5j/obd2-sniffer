/*
 * Lock-free queue of CAN frames between one producer and one consumer:
 * the CAN receive interrupt adds frames, the main loop takes them out.
 *
 * Built on the byte ring buffer (ring_buffer.h), which already provides the
 * lock-free ordering. Frames are stored as fixed-size records and only ever
 * written or read whole: a frame is added only if there is room for all of
 * it, and the producer publishes it with a single update of the ring's write
 * position, so the consumer can never see half a frame.
 */
#ifndef FRAME_QUEUE_H
#define FRAME_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include "can/can_frame.h"
#include "buffers/ring_buffer.h"

typedef struct {
    ring_buffer_t ring;
} frame_queue_t;

/* `capacity` is in bytes and must be a power of two; the queue holds
 * capacity / sizeof(can_frame_t) frames. False on bad arguments. */
bool frame_queue_init(frame_queue_t *q, uint8_t *storage, uint32_t capacity);

/* Producer: add a frame. False (nothing stored) if the queue is full. */
bool frame_queue_push(frame_queue_t *q, const can_frame_t *frame);

/* Consumer: take the oldest frame. False if the queue is empty. */
bool frame_queue_pop(frame_queue_t *q, can_frame_t *frame);

/* Frames waiting. Either side may call it. */
uint32_t frame_queue_count(const frame_queue_t *q);

/* Frames the queue can hold. */
uint32_t frame_queue_capacity(const frame_queue_t *q);

#endif /* FRAME_QUEUE_H */
