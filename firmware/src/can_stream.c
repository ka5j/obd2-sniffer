#include "can_stream.h"

#include <stdbool.h>

#include "can_drv.h"
#include "can_text.h"
#include "console.h"
#include "packet.h"
#include "uart_link.h"

/* Frames per pass of the main loop, so other tasks get their turn. */
#define FRAMES_PER_PASS 16U
/* With streaming off, frames are only taken out of the queue, which is
 * quick, so more go per pass. */
#define DISCARD_PER_PASS 64U
/* Interactive mode: after this long without frames, show the prompt again. */
#define QUIET_MS 200U

static bool enabled = true;
static uint32_t frames_streamed;
static bool lines_shown; /* frame lines printed since the prompt was last shown */
static uint32_t last_line_ms;

static void send_packet(const can_frame_t *frame)
{
    uint8_t packet[PACKET_CAN_FRAME_MAX];
    size_t n = packet_encode_can_frame(frame, packet, sizeof packet);
    (void)uart_link_write(packet, n, 0U); /* room was checked: never waits */
}

static void show_line(const can_frame_t *frame, uint32_t now_ms)
{
    char line[CAN_TEXT_MAX];
    if (!lines_shown) {
        console_puts("\r\n"); /* leave the prompt line */
        lines_shown = true;
    }
    size_t n = can_text_format(frame, line, sizeof line);
    console_write(line, n);
    last_line_ms = now_ms;
}

/* Send the next frame if there is one and its output fits, leaving the
 * space reserved for command replies. False if nothing was sent. */
static bool stream_one(bool interactive, uint32_t now_ms)
{
    /* The longest output one frame can produce, in either form. */
    size_t room = interactive ? (CAN_TEXT_MAX + 2U) : PACKET_CAN_FRAME_MAX;
    can_frame_t frame;

    if (uart_link_tx_free() < (CONSOLE_REPLY_SPACE + room)) {
        return false;
    }
    if (!can_drv_receive(&frame)) {
        return false;
    }
    if (interactive) {
        show_line(&frame, now_ms);
    } else {
        send_packet(&frame);
    }
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

void can_stream_task(uint32_t now_ms)
{
    if (!enabled) {
        discard();
        return;
    }
    bool interactive = console_is_interactive();

    for (uint32_t i = 0U; i < FRAMES_PER_PASS; i++) {
        if (!stream_one(interactive, now_ms)) {
            break;
        }
    }

    if (lines_shown && ((now_ms - last_line_ms) >= QUIET_MS)) {
        lines_shown = false;
        console_redraw_prompt();
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
