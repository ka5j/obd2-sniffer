#include "can_gen.h"

#include "can_drv.h"
#include "timebase.h"

#define US_PER_S 1000000U

static bool running;
static uint32_t total;
static uint32_t frames_sent;
static uint32_t period_us; /* 0: as fast as possible */
static uint32_t next_due_us;

bool can_gen_start(uint32_t count, uint32_t per_second)
{
    if ((can_drv_mode() != CAN_DRV_LOOPBACK) || (count == 0U) || (count > CAN_GEN_MAX_COUNT) ||
        (per_second > US_PER_S)) {
        return false;
    }
    total = count;
    frames_sent = 0U;
    period_us = (per_second == 0U) ? 0U : (US_PER_S / per_second);
    next_due_us = timebase_us();
    running = true;
    return true;
}

void can_gen_stop(void)
{
    running = false;
}

bool can_gen_running(void)
{
    return running;
}

uint32_t can_gen_sent(void)
{
    return frames_sent;
}

uint32_t can_gen_count(void)
{
    return total;
}

static can_frame_t numbered_frame(uint32_t n)
{
    can_frame_t f = {.id = CAN_GEN_ID, .dlc = CAN_MAX_DLC};
    for (uint32_t i = 0U; i < 4U; i++) {
        f.data[i] = (uint8_t)((n >> (8U * i)) & 0xFFU);
        f.data[i + 4U] = (uint8_t)((~n >> (8U * i)) & 0xFFU);
    }
    return f;
}

/* Has the time for the next frame come? The unsigned difference is below
 * 2^31 when "now" is at or after the due time, even when the microsecond
 * counter has wrapped around in between. */
static bool due(void)
{
    return (period_us == 0U) || ((timebase_us() - next_due_us) < 0x80000000U);
}

void can_gen_task(void)
{
    if (!running) {
        return;
    }
    if (can_drv_mode() != CAN_DRV_LOOPBACK) {
        running = false; /* CAN was switched off */
        return;
    }
    /* Fill free mailboxes, at most three per pass, so the loop stays quick. */
    while ((frames_sent < total) && due() && (can_drv_tx_free() > 0U)) {
        can_frame_t f = numbered_frame(frames_sent);
        if (!can_drv_send(&f)) {
            break;
        }
        frames_sent++;
        next_due_us += period_us;
    }
    if (frames_sent >= total) {
        running = false;
    }
}
