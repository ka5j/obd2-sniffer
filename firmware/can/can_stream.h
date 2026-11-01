/*
 * Sends received CAN frames to the laptop, one binary packet each
 * (lib/link/packet).
 *
 * It never waits for the serial link. A frame is only taken from the receive
 * queue once its output fits in the transmit buffer beyond the space kept
 * for command replies (CONSOLE_REPLY_SPACE), so commands stay responsive
 * while traffic streams. If the link can't keep up, frames wait in the
 * queue; if the queue fills up, the receive interrupt counts them as
 * dropped.
 *
 * Streaming can be switched off (`can stream off`) when the laptop doesn't
 * want the frames, or to measure what the board can capture without the
 * serial link as the limit. Frames are then still received and counted, but
 * not sent.
 */
#ifndef CAN_STREAM_H
#define CAN_STREAM_H

#include <stdbool.h>
#include <stdint.h>

/* Main loop: send what fits. */
void can_stream_task(void);

/* Frames sent to the laptop since start-up. */
uint32_t can_stream_sent(void);

/* Streaming on or off. On at start-up. */
void can_stream_enable(bool on);
bool can_stream_enabled(void);

#endif /* CAN_STREAM_H */
