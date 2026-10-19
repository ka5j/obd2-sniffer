/*
 * Sends received CAN frames to the laptop: one binary packet each in host
 * mode (lib/packet), one readable line each in interactive mode
 * (lib/can_text).
 *
 * It never waits for the serial link. A frame is only taken from the receive
 * queue once its output fits in the transmit buffer beyond the space kept
 * for command replies (CONSOLE_REPLY_SPACE), so commands stay responsive
 * while traffic streams. If the link can't keep up, frames wait in the
 * queue; if the queue fills up, the receive interrupt counts them as
 * dropped.
 */
#ifndef CAN_STREAM_H
#define CAN_STREAM_H

#include <stdint.h>

/* Main loop: send what fits. */
void can_stream_task(uint32_t now_ms);

/* Frames sent to the laptop since start-up. */
uint32_t can_stream_sent(void);

#endif /* CAN_STREAM_H */
