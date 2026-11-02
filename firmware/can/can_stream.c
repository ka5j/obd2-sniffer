#include "can/can_stream.h"

#include <stdbool.h>

#include "can/can_drv.h"
#include "console/console.h"
#include "link/packet.h"
#include "console/uart_link.h"

/* Frames per pass of the main loop, so other tasks get their turn. */
#define FRAMES_PER_PASS 16U
/* With streaming off, frames are only taken out of the queue, which is
 * quick, so more go per pass. */
#define DISCARD_PER_PASS 64U

static bool enabled = true;
static uint32_t frames_streamed;

/* Send the next frame if there is one and its packet fits, leaving the
 * space reserved for command replies. False if nothing was sent. */
static bool stream_one(void)
{
    can_frame_t frame;

    if (uart_link_tx_free() < (CONSOLE_REPLY_SPACE + PACKET_CAN_FRAME_MAX)) {
        return false;
    }
    if (!can_drv_receive(&frame)) {
        return false;
    }
    uint8_t packet[PACKET_CAN_FRAME_MAX];
    size_t n = packet_encode_can_frame(&frame, packet, sizeof packet);
    (void)uart_link_write(packet, n, 0U); /* room was checked: never waits */
    frames_streamed++;
    return true;
}

/* Streaming off: take frames out of the queue without sending them, so it
 * never fills up and frames aren't counted as dropped. */
static void discard(void)
{
    can_frame_t frame;
    for (uint32_t i = 0U; i < DISCARD_PER_PASS; i++) {
        if (!can_drv_receive(&frame)) {
            break;
        }
    }
}

void can_stream_task(void)
{
    if (!enabled) {
        discard();
        return;
    }
    for (uint32_t i = 0U; i < FRAMES_PER_PASS; i++) {
        if (!stream_one()) {
            break;
        }
    }
}

uint32_t can_stream_sent(void)
{
    return frames_streamed;
}

void can_stream_enable(bool on)
{
    enabled = on;
}

bool can_stream_enabled(void)
{
    return enabled;
}
