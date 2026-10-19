#include "can_text.h"

#include <stdint.h>

#define US_PER_S        1000000U
#define STD_ID_DIGITS   3U
#define EXT_ID_DIGITS   8U
#define ID_COLUMN_WIDTH 10U /* 8 hex digits and 2 spaces */
#define REMOTE_TEXT     "remote request"

static const char HEX_DIGITS[] = "0123456789ABCDEF";

static size_t put_char(char *out, size_t pos, char c)
{
    out[pos] = c;
    return pos + 1U;
}

static size_t put_text(char *out, size_t pos, const char *text)
{
    size_t p = pos;
    for (size_t i = 0U; text[i] != '\0'; i++) {
        p = put_char(out, p, text[i]);
    }
    return p;
}

/* `digits` hex digits of `value`, most significant first. */
static size_t put_hex(char *out, size_t pos, uint32_t value, uint32_t digits)
{
    size_t p = pos;
    for (uint32_t i = digits; i > 0U; i--) {
        uint32_t nibble = (value >> ((i - 1U) * 4U)) & 0xFU;
        p = put_char(out, p, HEX_DIGITS[nibble]);
    }
    return p;
}

/* Decimal, without leading zeros unless `min_digits` asks for them. */
static size_t put_decimal(char *out, size_t pos, uint32_t value, uint32_t min_digits)
{
    char digits[10];
    uint32_t count = 0U;
    uint32_t v = value;
    do {
        digits[count] = HEX_DIGITS[v % 10U]; /* also the decimal digits */
        count++;
        v /= 10U;
    } while (v != 0U);
    while (count < min_digits) {
        digits[count] = '0';
        count++;
    }
    size_t p = pos;
    while (count > 0U) {
        count--;
        p = put_char(out, p, digits[count]);
    }
    return p;
}

size_t can_text_format(const can_frame_t *frame, char *out, size_t size)
{
    if ((size < CAN_TEXT_MAX) || (frame->dlc > CAN_MAX_DLC)) {
        return 0U;
    }
    size_t p = put_text(out, 0U, "  ");
    p = put_decimal(out, p, frame->timestamp_us / US_PER_S, 1U);
    p = put_char(out, p, '.');
    p = put_decimal(out, p, frame->timestamp_us % US_PER_S, 6U);
    p = put_text(out, p, "  ");

    uint32_t id_digits = ((frame->flags & CAN_FLAG_EXTENDED) != 0U) ? EXT_ID_DIGITS : STD_ID_DIGITS;
    p = put_hex(out, p, frame->id, id_digits);
    for (uint32_t i = id_digits; i < ID_COLUMN_WIDTH; i++) {
        p = put_char(out, p, ' ');
    }

    p = put_char(out, p, '[');
    p = put_char(out, p, HEX_DIGITS[frame->dlc]);
    p = put_char(out, p, ']');
    if ((frame->flags & CAN_FLAG_RTR) != 0U) {
        p = put_text(out, p, "  " REMOTE_TEXT);
    } else {
        for (uint32_t i = 0U; i < frame->dlc; i++) {
            /* Two spaces before the data, one between bytes. */
            p = put_text(out, p, (i == 0U) ? "  " : " ");
            p = put_hex(out, p, frame->data[i], 2U);
        }
    }
    p = put_text(out, p, "\r\n");
    out[p] = '\0';
    return p;
}
