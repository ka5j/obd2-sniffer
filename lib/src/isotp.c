#include "isotp.h"

#include <stddef.h>

/* Frame kind: the high nibble of the first byte. */
#define PCI_SF 0x0U
#define PCI_FF 0x1U
#define PCI_CF 0x2U
#define PCI_FC 0x3U

/* Flow status: the low nibble of a flow-control frame's first byte. */
#define FS_CONTINUE 0x0U
#define FS_WAIT     0x1U
#define FS_OVERFLOW 0x2U

#define FC_LEN  3U /* bytes a flow-control frame must have */
#define FF_DATA 6U /* data bytes in a first frame */
#define CF_DATA 7U /* data bytes in a consecutive frame */
#define SN_MASK 0x0FU

/* STmin encoding: 0x00-0x7F are milliseconds, 0xF1-0xF9 are 100-900
 * microseconds. For any other (reserved) value the standard says to use
 * the longest valid gap, 127 ms. */
#define ST_MIN_MS_MAX      0x7FU
#define ST_MIN_100US_FIRST 0xF1U
#define ST_MIN_100US_LAST  0xF9U
#define ST_MIN_100US_BASE  0xF0U
#define ST_MIN_RESERVED_US 127000U

/* True once `now` has reached `at`. Works across the counter wrapping, for
 * times less than half its range apart (35 minutes at 1 MHz). */
static bool time_reached(uint32_t now, uint32_t at)
{
    return ((now - at) & 0x80000000U) == 0U;
}

static uint8_t pci_type(const uint8_t *data)
{
    return (uint8_t)(data[0] >> 4U);
}

static uint8_t pci_low(const uint8_t *data)
{
    return (uint8_t)(data[0] & 0x0FU);
}

static void pad(uint8_t *out, uint8_t from)
{
    for (uint8_t i = from; i < ISOTP_FRAME_LEN; i++) {
        out[i] = ISOTP_PADDING;
    }
}

static uint32_t st_min_to_us(uint8_t raw)
{
    if (raw <= ST_MIN_MS_MAX) {
        return (uint32_t)raw * 1000U;
    }
    if ((raw >= ST_MIN_100US_FIRST) && (raw <= ST_MIN_100US_LAST)) {
        return ((uint32_t)raw - ST_MIN_100US_BASE) * 100U;
    }
    return ST_MIN_RESERVED_US;
}

static void flow_control(uint8_t *out, uint8_t status, uint8_t block_size, uint8_t st_min)
{
    out[0] = (uint8_t)((PCI_FC << 4U) | status);
    out[1] = block_size;
    out[2] = st_min;
    pad(out, FC_LEN);
}

/* ---- receiver ------------------------------------------------------------ */

void isotp_rx_init(isotp_rx_t *rx, uint8_t *buf, uint16_t size, uint8_t block_size, uint8_t st_min,
                   uint32_t timeout_us)
{
    rx->buf = buf;
    rx->size = size;
    rx->length = 0U;
    rx->received = 0U;
    rx->timeout_us = timeout_us;
    rx->deadline_us = 0U;
    rx->block_size = block_size;
    rx->st_min = st_min;
    rx->block_left = 0U;
    rx->next_sn = 0U;
    rx->state = ISOTP_RX_IDLE;
}

static void rx_store(isotp_rx_t *rx, const uint8_t *src, uint8_t count)
{
    for (uint8_t i = 0U; i < count; i++) {
        rx->buf[rx->received] = src[i];
        rx->received++;
    }
}

static isotp_result_t rx_single(isotp_rx_t *rx, const uint8_t *data, uint8_t avail)
{
    uint8_t length = pci_low(data);
    if ((length == 0U) || (length >= avail)) {
        return ISOTP_OK; /* malformed, or shorter than it claims: ignored */
    }
    rx->state = ISOTP_RX_IDLE; /* abandons any message in progress */
    if (length > rx->size) {
        return ISOTP_ERR_OVERFLOW;
    }
    rx->length = length;
    rx->received = 0U;
    rx_store(rx, &data[1], length);
    return ISOTP_DONE;
}

static isotp_result_t rx_first(isotp_rx_t *rx, const uint8_t *data, uint8_t avail)
{
    if (avail < ISOTP_FRAME_LEN) {
        return ISOTP_OK; /* a first frame is always full: ignored */
    }
    uint16_t length = (uint16_t)((uint16_t)pci_low(data) << 8U) | data[1];
    if (length <= ISOTP_SF_MAX) {
        return ISOTP_OK; /* fits a single frame, so not a valid first frame: ignored */
    }
    if (length > rx->size) {
        rx->state = ISOTP_RX_SEND_OVERFLOW;
        return ISOTP_ERR_OVERFLOW;
    }
    rx->length = length;
    rx->received = 0U;
    rx_store(rx, &data[2], FF_DATA);
    rx->next_sn = 1U;
    rx->state = ISOTP_RX_SEND_FC;
    return ISOTP_OK;
}

static isotp_result_t rx_consecutive(isotp_rx_t *rx, const uint8_t *data, uint8_t avail,
                                     uint32_t now_us)
{
    if (rx->state != ISOTP_RX_RECEIVING) {
        return ISOTP_OK; /* not expecting one: ignored */
    }
    uint16_t left = rx->length - rx->received;
    uint8_t count = (left < CF_DATA) ? (uint8_t)left : CF_DATA;
    if (count >= avail) {
        return ISOTP_OK; /* shorter than the bytes still expected: ignored */
    }
    if (pci_low(data) != rx->next_sn) {
        rx->state = ISOTP_RX_IDLE;
        return ISOTP_ERR_SEQUENCE;
    }
    rx_store(rx, &data[1], count);
    rx->next_sn = (uint8_t)((rx->next_sn + 1U) & SN_MASK);
    if (rx->received == rx->length) {
        rx->state = ISOTP_RX_IDLE;
        return ISOTP_DONE;
    }
    if (rx->block_size != 0U) {
        rx->block_left--;
        if (rx->block_left == 0U) {
            rx->state = ISOTP_RX_SEND_FC; /* end of the block: ask for the next one */
            return ISOTP_OK;
        }
    }
    rx->deadline_us = now_us + rx->timeout_us;
    return ISOTP_OK;
}

isotp_result_t isotp_rx_frame(isotp_rx_t *rx, const uint8_t *data, uint8_t dlc, uint32_t now_us)
{
    uint8_t avail = (dlc > ISOTP_FRAME_LEN) ? ISOTP_FRAME_LEN : dlc;
    if (avail == 0U) {
        return ISOTP_OK;
    }
    isotp_result_t result = ISOTP_OK;
    switch (pci_type(data)) {
    case PCI_SF:
        result = rx_single(rx, data, avail);
        break;
    case PCI_FF:
        result = rx_first(rx, data, avail);
        break;
    case PCI_CF:
        result = rx_consecutive(rx, data, avail, now_us);
        break;
    default:
        break; /* flow control is for the sender; other kinds are reserved */
    }
    return result;
}

isotp_result_t isotp_rx_poll(isotp_rx_t *rx, uint32_t now_us, uint8_t out[ISOTP_FRAME_LEN])
{
    isotp_result_t result = ISOTP_OK;
    switch (rx->state) {
    case ISOTP_RX_SEND_FC:
        flow_control(out, FS_CONTINUE, rx->block_size, rx->st_min);
        result = ISOTP_SEND;
        break;
    case ISOTP_RX_SEND_OVERFLOW:
        flow_control(out, FS_OVERFLOW, 0U, 0U);
        result = ISOTP_SEND;
        break;
    case ISOTP_RX_RECEIVING:
        if (time_reached(now_us, rx->deadline_us)) {
            rx->state = ISOTP_RX_IDLE;
            result = ISOTP_ERR_TIMEOUT;
        }
        break;
    default:
        break;
    }
    return result;
}

void isotp_rx_sent(isotp_rx_t *rx, uint32_t now_us)
{
    if (rx->state == ISOTP_RX_SEND_FC) {
        rx->block_left = rx->block_size;
        rx->deadline_us = now_us + rx->timeout_us;
        rx->state = ISOTP_RX_RECEIVING;
    } else if (rx->state == ISOTP_RX_SEND_OVERFLOW) {
        rx->state = ISOTP_RX_IDLE;
    } else {
        /* nothing was waiting to be sent */
    }
}

/* ---- sender -------------------------------------------------------------- */

void isotp_tx_init(isotp_tx_t *tx, uint32_t timeout_us)
{
    tx->msg = NULL;
    tx->length = 0U;
    tx->sent = 0U;
    tx->timeout_us = timeout_us;
    tx->deadline_us = 0U;
    tx->st_min_us = 0U;
    tx->block_size = 0U;
    tx->block_left = 0U;
    tx->next_sn = 0U;
    tx->waits = 0U;
    tx->cf_ready = false;
    tx->state = ISOTP_TX_IDLE;
}

bool isotp_tx_start(isotp_tx_t *tx, const uint8_t *msg, uint16_t length)
{
    if ((length == 0U) || (length > ISOTP_MESSAGE_MAX)) {
        return false;
    }
    tx->msg = msg;
    tx->length = length;
    tx->sent = 0U;
    tx->next_sn = 1U;
    tx->waits = 0U;
    tx->cf_ready = false;
    tx->state = ISOTP_TX_SEND_FIRST;
    return true;
}

/* Bytes the next consecutive frame carries. */
static uint8_t tx_cf_count(const isotp_tx_t *tx)
{
    uint16_t left = tx->length - tx->sent;
    return (left < CF_DATA) ? (uint8_t)left : CF_DATA;
}

/* The next `count` bytes of the message into out[pos...], then padding. */
static void tx_fill(const isotp_tx_t *tx, uint8_t *out, uint8_t pos, uint8_t count)
{
    for (uint8_t i = 0U; i < count; i++) {
        out[pos + i] = tx->msg[tx->sent + i];
    }
    pad(out, pos + count);
}

isotp_result_t isotp_tx_frame(isotp_tx_t *tx, const uint8_t *data, uint8_t dlc, uint32_t now_us)
{
    if ((tx->state != ISOTP_TX_WAIT_FC) || (dlc < FC_LEN) || (pci_type(data) != PCI_FC)) {
        return ISOTP_OK;
    }
    isotp_result_t result = ISOTP_OK;
    switch (pci_low(data)) {
    case FS_CONTINUE:
        tx->block_size = data[1];
        tx->block_left = data[1];
        tx->st_min_us = st_min_to_us(data[2]);
        tx->waits = 0U;
        tx->deadline_us = now_us; /* the first consecutive frame may go at once */
        tx->state = ISOTP_TX_SENDING;
        break;
    case FS_WAIT:
        tx->waits++;
        if (tx->waits > ISOTP_MAX_WAITS) {
            tx->state = ISOTP_TX_IDLE;
            result = ISOTP_ERR_PROTOCOL;
        } else {
            tx->deadline_us = now_us + tx->timeout_us;
        }
        break;
    case FS_OVERFLOW:
        tx->state = ISOTP_TX_IDLE;
        result = ISOTP_ERR_OVERFLOW;
        break;
    default:
        tx->state = ISOTP_TX_IDLE;
        result = ISOTP_ERR_PROTOCOL;
        break;
    }
    return result;
}

isotp_result_t isotp_tx_poll(isotp_tx_t *tx, uint32_t now_us, uint8_t out[ISOTP_FRAME_LEN])
{
    isotp_result_t result = ISOTP_OK;
    switch (tx->state) {
    case ISOTP_TX_SEND_FIRST:
        if (tx->length <= ISOTP_SF_MAX) {
            out[0] = (uint8_t)tx->length; /* PCI_SF is 0 */
            tx_fill(tx, out, 1U, out[0]);
        } else {
            out[0] = (uint8_t)((PCI_FF << 4U) | (tx->length >> 8U));
            out[1] = (uint8_t)(tx->length & 0xFFU);
            tx_fill(tx, out, 2U, FF_DATA);
        }
        result = ISOTP_SEND;
        break;
    case ISOTP_TX_WAIT_FC:
        if (time_reached(now_us, tx->deadline_us)) {
            tx->state = ISOTP_TX_IDLE;
            result = ISOTP_ERR_TIMEOUT;
        }
        break;
    case ISOTP_TX_SENDING:
        if (time_reached(now_us, tx->deadline_us)) {
            out[0] = (uint8_t)((PCI_CF << 4U) | tx->next_sn);
            tx_fill(tx, out, 1U, tx_cf_count(tx));
            tx->cf_ready = true;
            result = ISOTP_SEND;
        }
        break;
    default:
        break;
    }
    return result;
}

static isotp_result_t tx_cf_sent(isotp_tx_t *tx, uint32_t now_us)
{
    tx->cf_ready = false;
    tx->sent += tx_cf_count(tx);
    tx->next_sn = (uint8_t)((tx->next_sn + 1U) & SN_MASK);
    if (tx->sent == tx->length) {
        tx->state = ISOTP_TX_IDLE;
        return ISOTP_DONE;
    }
    if (tx->block_size != 0U) {
        tx->block_left--;
        if (tx->block_left == 0U) {
            tx->deadline_us = now_us + tx->timeout_us; /* end of the block: wait for flow control */
            tx->state = ISOTP_TX_WAIT_FC;
            return ISOTP_OK;
        }
    }
    tx->deadline_us = now_us + tx->st_min_us; /* STmin counts from the end of the last frame */
    return ISOTP_OK;
}

isotp_result_t isotp_tx_sent(isotp_tx_t *tx, uint32_t now_us)
{
    isotp_result_t result = ISOTP_OK;
    if (tx->state == ISOTP_TX_SEND_FIRST) {
        if (tx->length <= ISOTP_SF_MAX) {
            tx->sent = tx->length;
            tx->state = ISOTP_TX_IDLE;
            result = ISOTP_DONE;
        } else {
            tx->sent = FF_DATA;
            tx->deadline_us = now_us + tx->timeout_us;
            tx->state = ISOTP_TX_WAIT_FC;
        }
    } else if ((tx->state == ISOTP_TX_SENDING) && tx->cf_ready) {
        result = tx_cf_sent(tx, now_us);
    } else {
        /* nothing was waiting to be sent */
    }
    return result;
}

bool isotp_tx_busy(const isotp_tx_t *tx)
{
    return tx->state != ISOTP_TX_IDLE;
}
