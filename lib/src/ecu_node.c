#include "ecu_node.h"

#include <stddef.h>
#include <string.h>

/* ISO 15765-2's default limit for the wait for flow control (N_Bs). */
#define FRAME_TIMEOUT_US 1000000U

void ecu_node_init(ecu_node_t *node)
{
    (void)memset(node, 0, sizeof(*node));
    /* OBD2 requests are single frames, so the request buffer holds one:
     * anything longer is answered with "overflow" flow control. */
    isotp_rx_init(&node->rx, node->request, ISOTP_SF_MAX, 0U, 0U, FRAME_TIMEOUT_US);
    isotp_tx_init(&node->tx, FRAME_TIMEOUT_US);
    node->pending = ECU_NODE_SEND_NONE;
}

void ecu_node_frame(ecu_node_t *node, uint32_t id, const uint8_t *data, uint8_t dlc,
                    uint32_t now_us, uint32_t elapsed_ms)
{
    if ((id != ECU_NODE_FUNCTIONAL_ID) && (id != ECU_NODE_REQUEST_ID)) {
        return;
    }
    /* Flow control for a reply in progress. */
    if (isotp_tx_frame(&node->tx, data, dlc, now_us) != ISOTP_OK) {
        node->errors++; /* the tester refused the reply */
    }
    isotp_result_t result = isotp_rx_frame(&node->rx, data, dlc, now_us);
    if (result == ISOTP_DONE) {
        size_t len = ecu_sim_reply(elapsed_ms, node->request, node->rx.length, node->reply,
                                   sizeof(node->reply));
        if (len > 0U) {
            (void)isotp_tx_start(&node->tx, node->reply, (uint16_t)len); /* 1-20 bytes */
            node->answered++;
        } else {
            node->ignored++;
        }
    } else if (result != ISOTP_OK) {
        node->errors++; /* a request longer than a single frame */
    } else {
        /* not a request */
    }
}

bool ecu_node_poll(ecu_node_t *node, uint32_t now_us, uint32_t *id, uint8_t out[ISOTP_FRAME_LEN])
{
    node->pending = ECU_NODE_SEND_NONE;
    /* The receiver only ever has "overflow" flow control to send. */
    if (isotp_rx_poll(&node->rx, now_us, out) == ISOTP_SEND) {
        *id = ECU_NODE_REPLY_ID;
        node->pending = ECU_NODE_SEND_FLOW_CONTROL;
        return true;
    }
    isotp_result_t tx_result = isotp_tx_poll(&node->tx, now_us, out);
    if (tx_result == ISOTP_SEND) {
        *id = ECU_NODE_REPLY_ID;
        node->pending = ECU_NODE_SEND_REPLY;
        return true;
    }
    if (tx_result == ISOTP_ERR_TIMEOUT) {
        node->errors++; /* the tester never sent flow control */
    }
    return false;
}

void ecu_node_sent(ecu_node_t *node, uint32_t now_us)
{
    if (node->pending == ECU_NODE_SEND_FLOW_CONTROL) {
        isotp_rx_sent(&node->rx, now_us);
    } else if (node->pending == ECU_NODE_SEND_REPLY) {
        (void)isotp_tx_sent(&node->tx, now_us);
    } else {
        /* nothing was handed out */
    }
    node->pending = ECU_NODE_SEND_NONE;
}
