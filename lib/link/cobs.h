/*
 * COBS: Consistent Overhead Byte Stuffing.
 *
 * Rewrites a block of bytes so that it contains no zero byte, at a cost of
 * one extra byte per 254 (plus one). A zero byte can then mark where each
 * packet starts and ends in a byte stream: a receiver that joins mid-stream,
 * or loses a byte, finds the next packet at the next zero.
 *
 * The encoded form is a series of blocks. Each block starts with a code byte
 * N (1-255) followed by N-1 data bytes; a code below 255 stands for those
 * bytes followed by a zero, except at the very end of the packet.
 */
#ifndef COBS_H
#define COBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Largest encoded size for `len` input bytes. */
#define COBS_MAX_ENCODED(len) ((len) + ((len) / 254U) + 1U)

/* Encode `len` bytes into `out`. False if `out_size` is too small. */
bool cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t out_size, size_t *out_len);

/* Decode a packet (without its delimiters). False if it is not valid COBS
 * (empty, contains a zero, or a block runs past the end) or `out_size` is
 * too small. */
bool cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t out_size, size_t *out_len);

#endif /* COBS_H */
