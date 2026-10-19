/*
 * The diagnostic tester: asks the car's engine ECU for live data, its stored
 * trouble codes and its VIN, over OBD2 on CAN, and keeps the latest answers.
 *
 * It works through a fixed plan, one request at a time:
 *   - the VIN, until it has one (retried every few seconds if it fails);
 *   - the stored trouble codes and the monitor status (warning light, number
 *     of codes), every few seconds;
 *   - otherwise the live-data PIDs from obd2.h's table, in turn.
 * Each request waits for its reply or for the reply timeout, then the next
 * one follows after a short gap, which keeps the bus load modest.
 *
 * Requests are single frames sent to every ECU (0x7DF). The engine ECU
 * replies from 0x7E8. A reply longer than one frame (the VIN) arrives over
 * ISO-TP, and the client's flow control goes to the ECU's own ID (0x7E0).
 *
 * Like the ISO-TP state machines it is built on, it never waits: the caller
 * passes in the frames it receives and polls it with the time; when poll
 * gives a frame, the caller transmits it and calls obd_client_sent().
 */
#ifndef OBD_CLIENT_H
#define OBD_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "isotp.h"
#include "obd2.h"

#define OBD_CLIENT_REQUEST_ID 0x7DFU /* requests: to every ECU */
#define OBD_CLIENT_FC_ID      0x7E0U /* flow control: to the engine ECU */
#define OBD_CLIENT_REPLY_ID   0x7E8U /* replies from the engine ECU */
#define OBD_CLIENT_DTC_MAX    8U     /* trouble codes it keeps */
#define OBD_CLIENT_REPLY_MAX  64U    /* longest reply it accepts */

/* ISO 15765-4: an ECU must start its reply within 50 ms. */
#define OBD_CLIENT_REPLY_TIMEOUT_US 50000U
#define OBD_CLIENT_GAP_US           10000U   /* between a reply and the next request */
#define OBD_CLIENT_REFRESH_US       5000000U /* trouble codes and monitor status */
#define OBD_CLIENT_VIN_RETRY_US     5000000U

typedef enum {
    OBD_CLIENT_STOPPED,
    OBD_CLIENT_GAP,     /* waiting before the next request */
    OBD_CLIENT_SENDING, /* request handed out, not yet sent */
    OBD_CLIENT_WAITING  /* request sent, waiting for the reply */
} obd_client_state_t;

typedef enum {
    OBD_CLIENT_SEND_NONE,
    OBD_CLIENT_SEND_REQUEST,
    OBD_CLIENT_SEND_FLOW_CONTROL
} obd_client_send_t;

typedef struct {
    isotp_tx_t tx;
    isotp_rx_t rx;
    uint8_t reply[OBD_CLIENT_REPLY_MAX];
    uint8_t request[2];
    uint16_t request_len;
    obd_client_state_t state;
    obd_client_send_t pending; /* what the last poll handed out */
    uint32_t deadline_us;      /* end of the gap, or of the wait for a reply */
    uint32_t refresh_due_us;
    uint32_t vin_due_us;
    bool status_next; /* monitor status follows the trouble codes */
    uint8_t pid_index;

    /* Latest answers. */
    int32_t values[OBD2_PID_COUNT]; /* in obd2.h's table order */
    bool value_valid[OBD2_PID_COUNT];
    bool status_valid;
    bool mil_on;
    uint8_t dtc_reported; /* codes the ECU says it has (monitor status) */
    bool dtcs_valid;
    uint8_t dtc_count; /* codes kept, at most OBD_CLIENT_DTC_MAX */
    uint16_t dtcs[OBD_CLIENT_DTC_MAX];
    bool vin_valid;
    char vin[OBD2_VIN_LEN + 1U];

    /* Counters since the last start. */
    uint32_t requests;
    uint32_t replies;
    uint32_t timeouts;
    uint32_t errors; /* transport errors, negative or malformed replies */
} obd_client_t;

void obd_client_init(obd_client_t *c);

/* Start asking, beginning with the VIN. Forgets earlier answers and counters. */
void obd_client_start(obd_client_t *c, uint32_t now_us);

/* Stop asking. The answers so far are kept. */
void obd_client_stop(obd_client_t *c);

bool obd_client_running(const obd_client_t *c);

/* A frame from the bus; only replies from OBD_CLIENT_REPLY_ID matter. */
void obd_client_frame(obd_client_t *c, uint32_t id, const uint8_t *data, uint8_t dlc,
                      uint32_t now_us);

/* The next frame to transmit, if one is due: true with its CAN identifier in
 * `id` and its 8 bytes in `out`. Also handles the reply timeout. */
bool obd_client_poll(obd_client_t *c, uint32_t now_us, uint32_t *id, uint8_t out[ISOTP_FRAME_LEN]);

/* The frame from the last poll has been transmitted. */
void obd_client_sent(obd_client_t *c, uint32_t now_us);

#endif /* OBD_CLIENT_H */
