#include "console/line_reader.h"

#include <string.h>

/* ASCII codes the reader reacts to. Bytes are compared as unsigned char:
 * plain char is signed on x86/macOS hosts but unsigned on Arm, and the
 * behaviour must not depend on which. */
#define ASCII_CTRL_C          0x03U
#define ASCII_LF              0x0AU
#define ASCII_CR              0x0DU
#define ASCII_FIRST_PRINTABLE 0x20U /* space */
#define ASCII_LAST_PRINTABLE  0x7EU /* '~' */

void line_reader_init(line_reader_t *lr)
{
    (void)memset(lr, 0, sizeof(*lr));
}

line_event_t line_reader_feed(line_reader_t *lr, char c)
{
    unsigned char uc = (unsigned char)c;

    /* A completed line is consumed by the time the next byte arrives. */
    if (lr->ready) {
        lr->ready = false;
        lr->len = 0U;
    }

    bool after_cr = lr->last_was_cr;
    lr->last_was_cr = (uc == ASCII_CR);

    if ((uc == ASCII_CR) || (uc == ASCII_LF)) {
        if ((uc == ASCII_LF) && after_cr) {
            return LR_NONE; /* second half of CR LF */
        }
        if (lr->overflowed) {
            lr->overflowed = false;
            lr->len = 0U;
            return LR_TOO_LONG;
        }
        lr->buf[lr->len] = '\0';
        lr->ready = true;
        return LR_LINE_READY;
    }

    if (uc == ASCII_CTRL_C) {
        lr->len = 0U;
        lr->overflowed = false;
        return LR_NONE;
    }

    if ((uc < ASCII_FIRST_PRINTABLE) || (uc > ASCII_LAST_PRINTABLE)) {
        return LR_NONE; /* other control or non-ASCII bytes */
    }

    if (lr->len >= LINE_READER_MAX) {
        lr->overflowed = true; /* the byte is dropped; the line is rejected when it ends */
        return LR_NONE;
    }
    lr->buf[lr->len] = c;
    lr->len++;
    return LR_NONE;
}

const char *line_reader_line(const line_reader_t *lr)
{
    return lr->buf;
}
