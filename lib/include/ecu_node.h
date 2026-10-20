/*
 * The simulated ECU as a node on the CAN bus: ISO-TP around ecu_sim.h, so a
 * tester reaches it exactly as it would reach a car's engine ECU.
 *
 * It takes requests sent to every ECU (0x7DF) or to it alone (0x7E0) and
 * replies from 0x7E8. Flow control for a reply longer than one frame (the
 * VIN) comes back to 0x7E0.
 *
 * Like the tester (obd_client.h), it never waits: the caller passes in the
 * frames it receives and polls it with the time; when poll gives a frame,
 * the caller transmits it and calls ecu_node_sent().
 */
#ifndef ECU_NODE_H
#define ECU_NODE_H

#include <stdbool.h>
#include <stdint.h>

#include "ecu_sim.h"
#include "isotp.h"

#define ECU_NODE_FUNCTIONAL_ID 0x7DFU /* requests to every ECU */
#define ECU_NODE_REQUEST_ID    0x7E0U /* requests and flow control to this ECU */
#define ECU_NODE_REPLY_ID      0x7E8U /* everything it sends */

typedef enum {
    ECU_NODE_SEND_NONE,
    ECU_NODE_SEND_REPLY,
    ECU_NODE_SEND_FLOW_CONTROL
} ecu_node_send_t;

typedef struct {
    isotp_rx_t rx;
    isotp_tx_t tx;
    uint8_t request[ISOTP_SF_MAX];
    uint8_t reply[ECU_SIM_REPLY_MAX];
    ecu_node_send_t pending; /* what the last poll handed out */
    uint32_t answered;       /* requests replied to */
    uint32_t ignored;        /* requests it has no reply for */
    uint32_t errors;         /* transfers that failed */
} ecu_node_t;

void ecu_node_init(ecu_node_t *node);

/* A frame from the bus; frames to other identifiers are ignored.
 * `elapsed_ms` is the time since the simulation started, which sets the
 * values in the reply. */
void ecu_node_frame(ecu_node_t *node, uint32_t id, const uint8_t *data, uint8_t dlc,
                    uint32_t now_us, uint32_t elapsed_ms);

/* The next frame to transmit, if one is due: true with its CAN identifier in
 * `id` and its 8 bytes in `out`. */
bool ecu_node_poll(ecu_node_t *node, uint32_t now_us, uint32_t *id, uint8_t out[ISOTP_FRAME_LEN]);

/* The frame from the last poll has been transmitted. */
void ecu_node_sent(ecu_node_t *node, uint32_t now_us);

#endif /* ECU_NODE_H */
