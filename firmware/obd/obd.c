#include "obd/obd.h"

#include "can/can_drv.h"
#include "obd/ecu_node.h"
#include "system/timebase.h"

/* Frames handled per pass of the main loop. OBD2 traffic comes a few
 * frames per request, so this empties the queue every time. */
#define FRAMES_PER_PASS 8U

static obd_client_t tester;
static ecu_node_t ecu;

void obd_init(void)
{
    obd_client_init(&tester);
    ecu_node_init(&ecu);
}

static bool transmit(uint32_t id, const uint8_t *data)
{
    can_frame_t frame = {.id = id, .dlc = ISOTP_FRAME_LEN};
    for (uint32_t i = 0U; i < ISOTP_FRAME_LEN; i++) {
        frame.data[i] = data[i];
    }
    return can_drv_send(&frame);
}

/* Each side hands out its next frame on every poll until it has been
 * sent, so a frame that finds the transmit mailboxes full goes next time. */
static void send_from_ecu(void)
{
    uint32_t id = 0U;
    uint8_t out[ISOTP_FRAME_LEN];
    if (ecu_node_poll(&ecu, timebase_us(), &id, out)) {
        if (transmit(id, out)) {
            ecu_node_sent(&ecu, timebase_us());
        }
    }
}

static void send_from_tester(void)
{
    uint32_t id = 0U;
    uint8_t out[ISOTP_FRAME_LEN];
    if (obd_client_poll(&tester, timebase_us(), &id, out)) {
        if (transmit(id, out)) {
            obd_client_sent(&tester, timebase_us());
        }
    }
}

void obd_task(uint32_t now_ms)
{
    bool ecu_on = obd_ecu_on();
    can_frame_t frame;
    for (uint32_t i = 0U; i < FRAMES_PER_PASS; i++) {
        if (!can_drv_receive_obd(&frame)) {
            break;
        }
        uint32_t now_us = timebase_us();
        if (ecu_on) {
            /* The simulation's clock is the board's uptime. */
            ecu_node_frame(&ecu, frame.id, frame.data, frame.dlc, now_us, now_ms);
        }
        obd_client_frame(&tester, frame.id, frame.data, frame.dlc, now_us);
    }
    if (ecu_on) {
        send_from_ecu();
    }
    send_from_tester();
}

bool obd_start(void)
{
    if (can_drv_mode() != CAN_DRV_LOOPBACK) {
        return false;
    }
    obd_client_start(&tester, timebase_us());
    return true;
}

void obd_stop(void)
{
    obd_client_stop(&tester);
}

bool obd_ecu_on(void)
{
    return can_drv_mode() == CAN_DRV_LOOPBACK;
}

const obd_client_t *obd_tester(void)
{
    return &tester;
}
