#include "obd/tx_guard.h"

#include <stddef.h>
#include <string.h>

#include "obd/obd2.h"

#define ISOTP_PCI_TYPE_MASK 0xF0U
#define ISOTP_PCI_LEN_MASK  0x0FU
#define ISOTP_SINGLE_FRAME  0x00U
#define ISOTP_FC_CONTINUE   0x30U
#define ISOTP_FC_OVERFLOW   0x32U
#define ISOTP_FRAME_BYTES   8U

/* Service 01 PIDs that report what the ECU supports, besides the table's. */
#define PID_SUPPORTED_01_20 0x00U

static bool live_data_pid_allowed(uint8_t pid)
{
    return (pid == PID_SUPPORTED_01_20) || (pid == OBD2_PID_MONITOR_STATUS) ||
           (obd2_pid_find(pid) != NULL);
}

/* The payload of a single-frame request (`len` bytes after the PCI byte). */
static bool request_allowed(const uint8_t *payload, uint8_t len)
{
    uint8_t service = payload[0];
    if (service == OBD2_SERVICE_STORED_DTCS) {
        return len == 1U;
    }
    if (len != 2U) {
        return false;
    }
    if (service == OBD2_SERVICE_CURRENT_DATA) {
        return live_data_pid_allowed(payload[1]);
    }
    return (service == OBD2_SERVICE_VEHICLE_INFO) && (payload[1] == OBD2_INFO_VIN);
}

static bool is_request(const can_frame_t *frame)
{
    uint8_t pci = frame->data[0];
    uint8_t len = pci & ISOTP_PCI_LEN_MASK;
    return (frame->id == TX_GUARD_REQUEST_ID) &&
           ((pci & ISOTP_PCI_TYPE_MASK) == ISOTP_SINGLE_FRAME) && (len >= 1U) && (len <= 2U) &&
           request_allowed(&frame->data[1], len);
}

static bool is_flow_control(const can_frame_t *frame)
{
    uint8_t pci = frame->data[0];
    return (frame->id >= TX_GUARD_FC_ID_FIRST) && (frame->id <= TX_GUARD_FC_ID_LAST) &&
           ((pci == ISOTP_FC_CONTINUE) || (pci == ISOTP_FC_OVERFLOW));
}

void tx_guard_init(tx_guard_t *g)
{
    (void)memset(g, 0, sizeof(*g));
}

bool tx_guard_allows(tx_guard_t *g, const can_frame_t *frame, uint32_t now_us)
{
    bool allowed = false;
    if ((frame->flags == 0U) && (frame->dlc == ISOTP_FRAME_BYTES)) {
        if (is_flow_control(frame)) {
            allowed = true;
        } else if (is_request(frame)) {
            /* Unsigned subtraction stays right when the clock wraps. */
            allowed = !g->requested || ((now_us - g->last_request_us) >= TX_GUARD_REQUEST_GAP_US);
            if (allowed) {
                g->requested = true;
                g->last_request_us = now_us;
            }
        } else {
            /* Neither: refused below. */
        }
    }
    if (!allowed) {
        g->refused++;
    }
    return allowed;
}
