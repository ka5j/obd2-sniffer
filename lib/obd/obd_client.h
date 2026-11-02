/*
 * The diagnostic tester: asks the car's engine ECU one question at a time,
 * over OBD2 on CAN, and keeps the latest answers.
 *
 * It never asks on its own. Each question comes from the laptop
 * (obd_client_ask()): a live value from obd2.h's table, the monitor status
 * (warning light, number of codes), the stored trouble codes or the VIN.
 * Deciding what to ask, and how often, is the laptop's job; if the laptop
 * stops asking, the board stops sending.
 *
 * One request is in progress at a time, and the next goes out no sooner
 * than OBD_CLIENT_GAP_US after the last, so the client always stays within
 * the transmit guard's rate limit (lib/obd/tx_guard).
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

#include "obd/isotp.h"
#include "obd/obd2.h"

#define OBD_CLIENT_REQUEST_ID 0x7DFU /* requests: to every ECU */
#define OBD_CLIENT_FC_ID      0x7E0U /* flow control: to the engine ECU */
#define OBD_CLIENT_REPLY_ID   0x7E8U /* replies from the engine ECU */
#define OBD_CLIENT_DTC_MAX    8U     /* trouble codes it keeps */
#define OBD_CLIENT_REPLY_MAX  64U    /* longest reply it accepts */

/* ISO 15765-4: an ECU must start its reply within 50 ms. */
#define OBD_CLIENT_REPLY_TIMEOUT_US 50000U
#define OBD_CLIENT_GAP_US           50000U /* between two requests: the guard's limit */

typedef enum {
    OBD_CLIENT_IDLE,    /* no request in progress */
    OBD_CLIENT_SENDING, /* asked: handed out once the gap has passed, until sent */
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
    uint32_t deadline_us;      /* end of the wait for a reply */
    bool sent_any;             /* a request has been sent since init */
    uint32_t last_sent_us;     /* when */

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

    /* Counters since start-up. */
    uint32_t requests;
    uint32_t replies;
    uint32_t timeouts;
    uint32_t errors; /* transport errors, negative or malformed replies */
} obd_client_t;

void obd_client_init(obd_client_t *c);

/* Ask one question: service 01 with a PID from obd2.h's table or the
 * monitor status, 03 (the PID is ignored), or 09 with the VIN's info type.
 * False if a request is still in progress or the question isn't one of
 * these. */
bool obd_client_ask(obd_client_t *c, uint8_t service, uint8_t pid);

/* True while a request is in progress. */
bool obd_client_busy(const obd_client_t *c);

/* Drop the request in progress, if any (for example when CAN stops). The
 * answers and counters are kept. */
void obd_client_cancel(obd_client_t *c);

/* A frame from the bus; only replies from OBD_CLIENT_REPLY_ID matter. */
void obd_client_frame(obd_client_t *c, uint32_t id, const uint8_t *data, uint8_t dlc,
                      uint32_t now_us);

/* The next frame to transmit, if one is due: true with its CAN identifier in
 * `id` and its 8 bytes in `out`. Also handles the reply timeout. */
bool obd_client_poll(obd_client_t *c, uint32_t now_us, uint32_t *id, uint8_t out[ISOTP_FRAME_LEN]);

/* The frame from the last poll has been transmitted. */
void obd_client_sent(obd_client_t *c, uint32_t now_us);

#endif /* OBD_CLIENT_H */
