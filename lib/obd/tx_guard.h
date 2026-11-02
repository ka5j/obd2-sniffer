/*
 * The transmit guard: the last check before any frame reaches the CAN bus.
 *
 * The board is a read-only scanner (docs/design/safety.md). It may send only
 * these frames, all 8 bytes long with 11-bit identifiers, as OBD2 on CAN
 * requires (ISO 15765-4):
 *
 *   to 0x7DF, a single-frame OBD2 read request:
 *     01 <pid>  live data, for the PIDs in obd2.h's table and 00 / 01
 *     03        stored trouble codes
 *     09 02     the VIN
 *   to 0x7E0-0x7E7, ISO-TP flow control for a reply on its way in:
 *     30 ...    continue to send
 *     32 ...    overflow: the reply is too long, stop
 *
 * Requests are rate limited: at least TX_GUARD_REQUEST_GAP_US between two
 * of them, so at most 20 a second. Flow control is part of receiving a
 * reply and is not limited.
 *
 * Everything else is refused and counted: other services (such as 04, which
 * clears trouble codes), other PIDs, other identifiers, 29-bit identifiers,
 * remote frames and short frames.
 */
#ifndef TX_GUARD_H
#define TX_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#include "can/can_frame.h"

#define TX_GUARD_REQUEST_ID     0x7DFU /* OBD2 requests: to every ECU */
#define TX_GUARD_FC_ID_FIRST    0x7E0U /* flow control: to one ECU */
#define TX_GUARD_FC_ID_LAST     0x7E7U
#define TX_GUARD_REQUEST_GAP_US 50000U /* at most 20 requests a second */

typedef struct {
    bool requested;           /* a request has been allowed since init */
    uint32_t last_request_us; /* when */
    uint32_t refused;         /* frames refused since init */
} tx_guard_t;

void tx_guard_init(tx_guard_t *g);

/* True if `frame` may be sent now. A request that is allowed starts the
 * gap before the next one; a refusal is counted. */
bool tx_guard_allows(tx_guard_t *g, const can_frame_t *frame, uint32_t now_us);

#endif /* TX_GUARD_H */
