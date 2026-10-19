#include "ring_buffer.h"

#include <string.h>

bool ring_init(ring_buffer_t *rb, uint8_t *storage, uint32_t capacity)
{
    if ((rb == NULL) || (storage == NULL) || (capacity < 2U) ||
        ((capacity & (capacity - 1U)) != 0U)) {
        return false;
    }
    rb->buf = storage;
    rb->mask = capacity - 1U;
    /* Not yet shared with another context, so relaxed stores suffice. */
    atomic_store_explicit(&rb->head, 0U, memory_order_relaxed);
    atomic_store_explicit(&rb->tail, 0U, memory_order_relaxed);
    return true;
}

uint32_t ring_capacity(const ring_buffer_t *rb)
{
    return rb->mask + 1U;
}

uint32_t ring_used(const ring_buffer_t *rb)
{
    /* Either side may call this. Read tail first: if the producer advances
     * head in between, the result is merely conservative (too small) for the
     * consumer, and vice versa. Unsigned subtraction handles counter wrap. */
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    return head - tail;
}

uint32_t ring_free(const ring_buffer_t *rb)
{
    return ring_capacity(rb) - ring_used(rb);
}

bool ring_is_empty(const ring_buffer_t *rb)
{
    return ring_used(rb) == 0U;
}

bool ring_put(ring_buffer_t *rb, uint8_t byte)
{
    return ring_write(rb, &byte, 1U) == 1U;
}

size_t ring_write(ring_buffer_t *rb, const uint8_t *data, size_t len)
{
    /* Producer owns head; only tail needs synchronising. */
    uint32_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    uint32_t space = ring_capacity(rb) - (head - tail);
    size_t count = (len > space) ? (size_t)space : len;

    /* Copy in at most two pieces: up to the end of storage, then from the
     * start. */
    uint32_t start = head & rb->mask;
    size_t first = (size_t)ring_capacity(rb) - (size_t)start;
    if (first > count) {
        first = count;
    }
    (void)memcpy(&rb->buf[start], data, first);
    (void)memcpy(&rb->buf[0], &data[first], count - first);

    /* Publish: data must be visible before the new head. */
    atomic_store_explicit(&rb->head, head + (uint32_t)count, memory_order_release);
    return count;
}

bool ring_get(ring_buffer_t *rb, uint8_t *byte)
{
    return ring_read(rb, byte, 1U) == 1U;
}

size_t ring_read(ring_buffer_t *rb, uint8_t *out, size_t len)
{
    /* Consumer owns tail; only head needs synchronising. */
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t avail = head - tail;
    size_t count = (len > avail) ? (size_t)avail : len;

    uint32_t start = tail & rb->mask;
    size_t first = (size_t)ring_capacity(rb) - (size_t)start;
    if (first > count) {
        first = count;
    }
    (void)memcpy(out, &rb->buf[start], first);
    (void)memcpy(&out[first], &rb->buf[0], count - first);

    /* Release the space only after the data has been copied out. */
    atomic_store_explicit(&rb->tail, tail + (uint32_t)count, memory_order_release);
    return count;
}

size_t ring_peek_contiguous(const ring_buffer_t *rb, const uint8_t **data)
{
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t avail = head - tail;
    uint32_t start = tail & rb->mask;
    uint32_t to_end = ring_capacity(rb) - start;

    *data = &rb->buf[start];
    return (avail < to_end) ? (size_t)avail : (size_t)to_end;
}

void ring_consume(ring_buffer_t *rb, size_t len)
{
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t avail = head - tail;
    /* Never move tail past head. */
    size_t count = (len > avail) ? (size_t)avail : len;

    atomic_store_explicit(&rb->tail, tail + (uint32_t)count, memory_order_release);
}
