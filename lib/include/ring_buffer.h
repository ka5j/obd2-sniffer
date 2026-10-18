/*
 * Lock-free single-producer / single-consumer byte ring buffer.
 *
 * One side (the producer) only writes, the other (the consumer) only reads,
 * e.g. a UART receive interrupt producing bytes for the main loop, or the
 * main loop producing bytes for a DMA transmit. With exactly one producer and
 * one consumer no locks or interrupt masking are needed:
 *
 *   - `head` is written only by the producer, `tail` only by the consumer.
 *   - Both are free-running 32-bit counters; the index into the storage is
 *     `counter & mask`. Unsigned wrap-around keeps `head - tail` correct.
 *   - The producer stores data *before* publishing the new head (release),
 *     and the consumer reads head (acquire) before touching data. The same
 *     ordering applies in reverse for tail. This is what makes it safe
 *     between an ISR and the main loop, or between two host threads.
 *
 * Capacity must be a power of two. All `capacity` bytes are usable.
 * When full, writes are truncated and the caller decides what to do with the
 * remainder (typically count it as dropped).
 */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    uint32_t mask;         /* capacity - 1 */
    _Atomic uint32_t head; /* total bytes ever written (producer) */
    _Atomic uint32_t tail; /* total bytes ever read (consumer) */
} ring_buffer_t;

/* Returns false if storage is NULL or capacity is not a power of two >= 2. */
bool ring_init(ring_buffer_t *rb, uint8_t *storage, uint32_t capacity);

uint32_t ring_capacity(const ring_buffer_t *rb);
uint32_t ring_used(const ring_buffer_t *rb);
uint32_t ring_free(const ring_buffer_t *rb);
bool ring_is_empty(const ring_buffer_t *rb);

/* ---- Producer side ---- */

/* Append one byte. Returns false (and stores nothing) if full. */
bool ring_put(ring_buffer_t *rb, uint8_t byte);

/* Append up to `len` bytes. Returns the number actually written. */
size_t ring_write(ring_buffer_t *rb, const uint8_t *data, size_t len);

/* ---- Consumer side ---- */

/* Remove one byte. Returns false if empty. */
bool ring_get(ring_buffer_t *rb, uint8_t *byte);

/* Remove up to `len` bytes into `out`. Returns the number actually read. */
size_t ring_read(ring_buffer_t *rb, uint8_t *out, size_t len);

/* Zero-copy read for DMA: point `*data` at the longest run of readable bytes
 * that is contiguous in memory (it stops at the end of the storage array) and
 * return its length. Nothing is removed until ring_consume() is called. */
size_t ring_peek_contiguous(const ring_buffer_t *rb, const uint8_t **data);

/* Remove `len` bytes previously exposed by ring_peek_contiguous(). */
void ring_consume(ring_buffer_t *rb, size_t len);

#endif /* RING_BUFFER_H */
