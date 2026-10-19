/*
 * ISO-TP (ISO 15765-2): messages longer than one CAN frame.
 *
 * A classic CAN frame carries at most 8 data bytes, but diagnostic messages
 * can be longer: the reply carrying a car's VIN is 20 bytes. ISO-TP splits a
 * message into frames and puts it back together. The first byte of each
 * frame (the protocol control information, PCI) says what kind it is:
 *
 *   single frame (SF)       the whole message, 1-7 bytes
 *   first frame (FF)        the total length (8-4095) and the first 6 bytes
 *   consecutive frame (CF)  the next 7 bytes, numbered 1, 2, ... 15, 0, 1, ...
 *   flow control (FC)       sent back by the receiver after the first frame:
 *                           continue, wait, or "too long for me"; how many
 *                           CFs to send before the next FC (the block size,
 *                           0 = all of them); and the minimum gap between CFs
 *                           (STmin)
 *
 * The receiver and the sender are separate state machines. Neither ever
 * waits: the caller hands them each received frame and calls ..._poll()
 * regularly with the time. When poll returns ISOTP_SEND, the caller
 * transmits `out` and calls ..._sent() once the frame has been accepted for
 * transmission. Until then poll returns the same frame again, so a busy
 * transmitter only delays a transfer and never loses a frame. This follows
 * the standard, where the data link layer confirms each transmission.
 *
 * Frames produced here are always 8 bytes, unused bytes padded with
 * ISOTP_PADDING, as OBD2 over CAN (ISO 15765-4) requires. Received frames
 * may be shorter, as long as they hold the bytes their PCI announces.
 *
 * Normal addressing: the PCI is the first byte and the caller picks the CAN
 * identifiers (for OBD2, requests on 0x7DF or 0x7E0 and replies on 0x7E8).
 * Frames from the other side's identifier can be given to both the receiver
 * and the sender: each one ignores the kinds of frame that aren't for it.
 */
#ifndef ISOTP_H
#define ISOTP_H

#include <stdbool.h>
#include <stdint.h>

#define ISOTP_FRAME_LEN   8U
#define ISOTP_SF_MAX      7U    /* longest message that fits in a single frame */
#define ISOTP_MESSAGE_MAX 4095U /* the first frame's 12-bit length field */
/* Fill for unused bytes: the value ISO 15765-2 recommends, chosen because
 * it needs the fewest stuff bits on the bus. */
#define ISOTP_PADDING 0xCCU
/* "Wait" flow-control frames the sender accepts for one message before
 * giving up (N_WFTmax in the standard), so a receiver can't stall it forever. */
#define ISOTP_MAX_WAITS 10U

typedef enum {
    ISOTP_OK,           /* nothing to report: idle, in progress, or a frame that doesn't apply */
    ISOTP_SEND,         /* poll: transmit `out`, then call ..._sent() */
    ISOTP_DONE,         /* receiver: a whole message is in the buffer; sender: all of it has gone */
    ISOTP_ERR_TIMEOUT,  /* the other side went quiet: no CF (receiver) or no FC (sender) in time */
    ISOTP_ERR_SEQUENCE, /* receiver: a consecutive frame was missing or out of order */
    ISOTP_ERR_OVERFLOW, /* the message is longer than the receiver's buffer */
    ISOTP_ERR_PROTOCOL  /* sender: a flow-control frame with an invalid status, or too many waits */
} isotp_result_t;

/* ---- receiver ------------------------------------------------------------ */

typedef enum {
    ISOTP_RX_IDLE,
    ISOTP_RX_SEND_FC,       /* first frame accepted: flow control "continue" to send */
    ISOTP_RX_SEND_OVERFLOW, /* first frame too long: flow control "overflow" to send */
    ISOTP_RX_RECEIVING      /* waiting for consecutive frames */
} isotp_rx_state_t;

typedef struct {
    uint8_t *buf;
    uint16_t size;        /* capacity of buf */
    uint16_t length;      /* message length, from its first or single frame */
    uint16_t received;    /* bytes of it received so far */
    uint32_t timeout_us;  /* longest wait for the next consecutive frame (N_Cr) */
    uint32_t deadline_us; /* when the current wait runs out */
    uint8_t block_size;   /* advertised in flow control: CFs per block, 0 = no limit */
    uint8_t st_min;       /* advertised in flow control, in the standard's encoding */
    uint8_t block_left;   /* CFs left in the current block */
    uint8_t next_sn;      /* sequence number the next CF must carry */
    isotp_rx_state_t state;
} isotp_rx_t;

/* Messages are assembled in `buf` (up to `size` bytes, at most
 * ISOTP_MESSAGE_MAX are ever used). `block_size` and `st_min` are what the
 * receiver asks the sender for: 0 and 0 mean "send everything as fast as
 * you can". */
void isotp_rx_init(isotp_rx_t *rx, uint8_t *buf, uint16_t size, uint8_t block_size, uint8_t st_min,
                   uint32_t timeout_us);

/* A frame from the sender. ISOTP_DONE: rx->length bytes are in the buffer,
 * valid until the next frame. An error abandons the message in progress.
 * Frames that don't fit the protocol state (a CF with no message in
 * progress, a malformed frame, flow control) are ignored. A new single or
 * first frame abandons any message in progress and starts again, as the
 * standard says. */
isotp_result_t isotp_rx_frame(isotp_rx_t *rx, const uint8_t *data, uint8_t dlc, uint32_t now_us);

/* ISOTP_SEND with a flow-control frame in `out`, ISOTP_ERR_TIMEOUT if the
 * sender stopped mid-message, otherwise ISOTP_OK. */
isotp_result_t isotp_rx_poll(isotp_rx_t *rx, uint32_t now_us, uint8_t out[ISOTP_FRAME_LEN]);

/* The flow-control frame from the last poll has been transmitted. */
void isotp_rx_sent(isotp_rx_t *rx, uint32_t now_us);

/* ---- sender -------------------------------------------------------------- */

typedef enum {
    ISOTP_TX_IDLE,
    ISOTP_TX_SEND_FIRST, /* single or first frame to send */
    ISOTP_TX_WAIT_FC,    /* first frame or a whole block sent: waiting for flow control */
    ISOTP_TX_SENDING     /* sending consecutive frames */
} isotp_tx_state_t;

typedef struct {
    const uint8_t *msg;
    uint16_t length;
    uint16_t sent;        /* bytes confirmed sent */
    uint32_t timeout_us;  /* longest wait for flow control (N_Bs) */
    uint32_t deadline_us; /* waiting for FC: give up then; sending: next CF allowed then */
    uint32_t st_min_us;   /* gap between CFs the receiver asked for */
    uint8_t block_size;   /* CFs per block the receiver asked for, 0 = no limit */
    uint8_t block_left;   /* CFs left in the current block */
    uint8_t next_sn;      /* sequence number of the next CF */
    uint8_t waits;        /* "wait" flow-control frames received in a row */
    bool cf_ready;        /* the last poll handed out a consecutive frame */
    isotp_tx_state_t state;
} isotp_tx_t;

void isotp_tx_init(isotp_tx_t *tx, uint32_t timeout_us);

/* Start sending `msg` (1 to ISOTP_MESSAGE_MAX bytes), abandoning any message
 * in progress. `msg` must stay valid until the transfer ends. False, and
 * nothing changes, if the length is out of range. */
bool isotp_tx_start(isotp_tx_t *tx, const uint8_t *msg, uint16_t length);

/* A frame from the receiver. Only flow control matters, and only while the
 * sender is waiting for it; anything else is ignored. */
isotp_result_t isotp_tx_frame(isotp_tx_t *tx, const uint8_t *data, uint8_t dlc, uint32_t now_us);

/* ISOTP_SEND with the next frame in `out` once it may go,
 * ISOTP_ERR_TIMEOUT if the receiver never sent flow control, otherwise
 * ISOTP_OK. */
isotp_result_t isotp_tx_poll(isotp_tx_t *tx, uint32_t now_us, uint8_t out[ISOTP_FRAME_LEN]);

/* The frame from the last poll has been transmitted. ISOTP_DONE when it
 * was the last one, otherwise ISOTP_OK. */
isotp_result_t isotp_tx_sent(isotp_tx_t *tx, uint32_t now_us);

/* A transfer is in progress. */
bool isotp_tx_busy(const isotp_tx_t *tx);

#endif /* ISOTP_H */
