#include "link/cobs.h"

#define COBS_MAX_CODE 0xFFU /* a block of 254 data bytes with no zero after it */

bool cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t out_size, size_t *out_len)
{
    if (out_size < 1U) {
        return false;
    }
    size_t code_pos = 0U; /* where the current block's code byte goes */
    size_t write = 1U;
    uint8_t code = 1U; /* 1 + data bytes in the current block */

    for (size_t i = 0U; i < len; i++) {
        if (in[i] != 0U) {
            if (write >= out_size) {
                return false;
            }
            out[write] = in[i];
            write++;
            code++;
        }
        /* A zero ends the block; so does a block that has reached its
         * maximum length, if more input follows. Either way a new block
         * starts. */
        if ((in[i] == 0U) || ((code == COBS_MAX_CODE) && ((i + 1U) < len))) {
            if (write >= out_size) {
                return false;
            }
            out[code_pos] = code;
            code_pos = write;
            write++;
            code = 1U;
        }
    }
    out[code_pos] = code;
    *out_len = write;
    return true;
}

bool cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t out_size, size_t *out_len)
{
    if (len == 0U) {
        return false;
    }
    size_t read = 0U;
    size_t write = 0U;

    while (read < len) {
        uint8_t code = in[read];
        if (code == 0U) {
            return false;
        }
        read++;
        for (uint8_t k = 1U; k < code; k++) {
            if ((read >= len) || (in[read] == 0U) || (write >= out_size)) {
                return false;
            }
            out[write] = in[read];
            write++;
            read++;
        }
        /* A short block stands for its data followed by a zero, unless it is
         * the last block of the packet. */
        if ((code != COBS_MAX_CODE) && (read < len)) {
            if (write >= out_size) {
                return false;
            }
            out[write] = 0U;
            write++;
        }
    }
    *out_len = write;
    return true;
}
