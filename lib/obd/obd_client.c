#include "obd/obd_client.h"

#include <stddef.h>
#include <string.h>

/* ISO 15765-2's default limit for the gap between consecutive frames of a
 * reply (N_Cr). Requests are single frames, so the sender's wait for flow
 * control (N_Bs) never applies; it gets the same value. */
#define FRAME_TIMEOUT_US 1000000U

#define CURRENT_DATA_REPLY (OBD2_SERVICE_CURRENT_DATA + OBD2_REPLY_OFFSET)
#define STORED_DTCS_REPLY  (OBD2_SERVICE_STORED_DTCS + OBD2_REPLY_OFFSET)
#define VEHICLE_INFO_REPLY (OBD2_SERVICE_VEHICLE_INFO + OBD2_REPLY_OFFSET)

/* True once `now` has reached `at`, across the counter wrapping. */
static bool reached(uint32_t now, uint32_t at)
{
    return ((now - at) & 0x80000000U) == 0U;
}

void obd_client_init(obd_client_t *c)
{
    (void)memset(c, 0, sizeof(*c));
    isotp_tx_init(&c->tx, FRAME_TIMEOUT_US);
    isotp_rx_init(&c->rx, c->reply, OBD_CLIENT_REPLY_MAX, 0U, 0U, FRAME_TIMEOUT_US);
    c->state = OBD_CLIENT_STOPPED;
    c->pending = OBD_CLIENT_SEND_NONE;
}

void obd_client_start(obd_client_t *c, uint32_t now_us)
{
    obd_client_init(c);
    c->state = OBD_CLIENT_GAP;
    c->deadline_us = now_us;
    c->refresh_due_us = now_us;
    c->vin_due_us = now_us;
}

void obd_client_stop(obd_client_t *c)
{
    isotp_tx_init(&c->tx, FRAME_TIMEOUT_US);
    isotp_rx_init(&c->rx, c->reply, OBD_CLIENT_REPLY_MAX, 0U, 0U, FRAME_TIMEOUT_US);
    c->state = OBD_CLIENT_STOPPED;
    c->pending = OBD_CLIENT_SEND_NONE;
}

bool obd_client_running(const obd_client_t *c)
{
    return c->state != OBD_CLIENT_STOPPED;
}

/* The request is over (answered, failed or timed out): pause, then ask the
 * next one. Replies that arrive when no request is waiting change nothing. */
static void end_request(obd_client_t *c, uint32_t now_us)
{
    if (c->state == OBD_CLIENT_WAITING) {
        c->state = OBD_CLIENT_GAP;
        c->deadline_us = now_us + OBD_CLIENT_GAP_US;
    }
}

static void choose_request(obd_client_t *c, uint32_t now_us)
{
    if (!c->vin_valid && reached(now_us, c->vin_due_us)) {
        c->request[0] = OBD2_SERVICE_VEHICLE_INFO;
        c->request[1] = OBD2_INFO_VIN;
        c->request_len = 2U;
        c->vin_due_us = now_us + OBD_CLIENT_VIN_RETRY_US;
    } else if (reached(now_us, c->refresh_due_us)) {
        c->request[0] = OBD2_SERVICE_STORED_DTCS;
        c->request_len = 1U;
        c->refresh_due_us = now_us + OBD_CLIENT_REFRESH_US;
        c->status_next = true;
    } else if (c->status_next) {
        c->request[0] = OBD2_SERVICE_CURRENT_DATA;
        c->request[1] = OBD2_PID_MONITOR_STATUS;
        c->request_len = 2U;
        c->status_next = false;
    } else {
        c->request[0] = OBD2_SERVICE_CURRENT_DATA;
        c->request[1] = obd2_pid_at(c->pid_index)->pid;
        c->request_len = 2U;
        c->pid_index = (uint8_t)((c->pid_index + 1U) % OBD2_PID_COUNT);
    }
}

static bool store_current_data(obd_client_t *c, uint8_t pid, const uint8_t *data, size_t len)
{
    if (pid == OBD2_PID_MONITOR_STATUS) {
        c->status_valid = obd2_decode_monitor_status(data, len, &c->mil_on, &c->dtc_reported);
        return c->status_valid;
    }
    for (size_t i = 0U; i < OBD2_PID_COUNT; i++) {
        const obd2_pid_t *info = obd2_pid_at(i);
        if (info->pid == pid) {
            c->value_valid[i] = obd2_decode(info, data, len, &c->values[i]);
            return c->value_valid[i];
        }
    }
    return false; /* a PID it doesn't ask for */
}

static bool store_dtcs(obd_client_t *c, const uint8_t *reply, size_t len)
{
    int32_t count = obd2_parse_dtcs(reply, len, c->dtcs, OBD_CLIENT_DTC_MAX);
    c->dtcs_valid = count >= 0;
    c->dtc_count = c->dtcs_valid ? (uint8_t)count : 0U;
    return c->dtcs_valid;
}

/* Keep what a complete reply says. False if it is negative, malformed or
 * of a kind the client never asks for. */
static bool store_reply(obd_client_t *c, const uint8_t *reply, size_t len)
{
    if (len < 2U) {
        return false;
    }
    bool stored = false;
    switch (reply[0]) {
    case CURRENT_DATA_REPLY:
        stored = store_current_data(c, reply[1], &reply[2], len - 2U);
        break;
    case STORED_DTCS_REPLY:
        stored = store_dtcs(c, reply, len);
        break;
    case VEHICLE_INFO_REPLY:
        c->vin_valid = obd2_parse_vin(reply, len, c->vin);
        stored = c->vin_valid;
        break;
    default:
        break; /* a negative reply (7F) or something unexpected */
    }
    return stored;
}

static void request_failed(obd_client_t *c, uint32_t now_us)
{
    c->errors++;
    end_request(c, now_us);
}

void obd_client_frame(obd_client_t *c, uint32_t id, const uint8_t *data, uint8_t dlc,
                      uint32_t now_us)
{
    if ((c->state == OBD_CLIENT_STOPPED) || (id != OBD_CLIENT_REPLY_ID)) {
        return;
    }
    isotp_result_t result = isotp_rx_frame(&c->rx, data, dlc, now_us);
    if (result == ISOTP_DONE) {
        if (store_reply(c, c->reply, c->rx.length)) {
            c->replies++;
        } else {
            c->errors++;
        }
        end_request(c, now_us);
    } else if (result != ISOTP_OK) {
        request_failed(c, now_us); /* reply out of sequence or too long */
    } else {
        /* part of a longer reply, or a frame that doesn't apply */
    }
}

bool obd_client_poll(obd_client_t *c, uint32_t now_us, uint32_t *id, uint8_t out[ISOTP_FRAME_LEN])
{
    c->pending = OBD_CLIENT_SEND_NONE;
    if (c->state == OBD_CLIENT_STOPPED) {
        return false;
    }
    /* Flow control for a reply in progress goes first. */
    isotp_result_t rx_result = isotp_rx_poll(&c->rx, now_us, out);
    if (rx_result == ISOTP_SEND) {
        *id = OBD_CLIENT_FC_ID;
        c->pending = OBD_CLIENT_SEND_FLOW_CONTROL;
        return true;
    }
    if (rx_result == ISOTP_ERR_TIMEOUT) {
        request_failed(c, now_us); /* the ECU stopped in the middle of a reply */
    }

    if (c->state == OBD_CLIENT_WAITING) {
        /* The reply must start in time; once it has, ISO-TP times the rest. */
        if ((c->rx.state == ISOTP_RX_IDLE) && reached(now_us, c->deadline_us)) {
            c->timeouts++;
            end_request(c, now_us);
        }
    } else if ((c->state == OBD_CLIENT_GAP) && reached(now_us, c->deadline_us)) {
        choose_request(c, now_us);
        (void)isotp_tx_start(&c->tx, c->request, c->request_len); /* 1-2 bytes: always valid */
        c->state = OBD_CLIENT_SENDING;
    } else {
        /* still in the gap, or a request already handed out */
    }

    if (c->state == OBD_CLIENT_SENDING) {
        /* A single frame: handed out again on every poll until it is sent. */
        (void)isotp_tx_poll(&c->tx, now_us, out);
        *id = OBD_CLIENT_REQUEST_ID;
        c->pending = OBD_CLIENT_SEND_REQUEST;
        return true;
    }
    return false;
}

void obd_client_sent(obd_client_t *c, uint32_t now_us)
{
    if (c->pending == OBD_CLIENT_SEND_FLOW_CONTROL) {
        isotp_rx_sent(&c->rx, now_us);
    } else if (c->pending == OBD_CLIENT_SEND_REQUEST) {
        (void)isotp_tx_sent(&c->tx, now_us); /* a single frame: the request is complete */
        c->requests++;
        c->state = OBD_CLIENT_WAITING;
        c->deadline_us = now_us + OBD_CLIENT_REPLY_TIMEOUT_US;
    } else {
        /* nothing was handed out */
    }
    c->pending = OBD_CLIENT_SEND_NONE;
}
