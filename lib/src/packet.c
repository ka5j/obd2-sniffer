#include "packet.h"

#include <stdbool.h>

#include "cobs.h"
#include "crc16.h"

/* type + seq + timestamp + id + dlc + data + crc */
#define BODY_MAX (1U + 2U + 4U + 4U + 1U + CAN_MAX_DLC + 2U)

_Static_assert(PACKET_CAN_FRAME_MAX == (COBS_MAX_ENCODED(BODY_MAX) + 2U),
               "PACKET_CAN_FRAME_MAX must match the largest encoded frame");

static size_t put_u16(uint8_t *buf, size_t pos, uint16_t value)
{
    buf[pos] = (uint8_t)(value & 0xFFU);
    buf[pos + 1U] = (uint8_t)(value >> 8U);
    return pos + 2U;
}

static size_t put_u32(uint8_t *buf, size_t pos, uint32_t value)
{
    for (uint32_t i = 0U; i < 4U; i++) {
        buf[pos + i] = (uint8_t)((value >> (8U * i)) & 0xFFU);
    }
    return pos + 4U;
}

size_t packet_encode_can_frame(const can_frame_t *frame, uint16_t seq, uint8_t *out, size_t size)
{
    if ((frame->dlc > CAN_MAX_DLC) || (size < 2U)) {
        return 0U;
    }
    bool rtr = (frame->flags & CAN_FLAG_RTR) != 0U;
    uint32_t id = frame->id;
    if ((frame->flags & CAN_FLAG_EXTENDED) != 0U) {
        id |= PACKET_CAN_ID_EXTENDED;
    }
    if (rtr) {
        id |= PACKET_CAN_ID_RTR;
    }

    uint8_t body[BODY_MAX];
    size_t n = 0U;
    body[n] = PACKET_TYPE_CAN_FRAME;
    n++;
    n = put_u16(body, n, seq);
    n = put_u32(body, n, frame->timestamp_us);
    n = put_u32(body, n, id);
    body[n] = frame->dlc;
    n++;
    uint8_t data_len = rtr ? 0U : frame->dlc;
    for (uint8_t i = 0U; i < data_len; i++) {
        body[n] = frame->data[i];
        n++;
    }
    n = put_u16(body, n, crc16(body, n));

    size_t encoded = 0U;
    if (!cobs_encode(body, n, &out[1], size - 2U, &encoded)) {
        return 0U;
    }
    out[0] = PACKET_DELIMITER;
    out[encoded + 1U] = PACKET_DELIMITER;
    return encoded + 2U;
}
