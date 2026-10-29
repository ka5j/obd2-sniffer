#include "console/line_editor.h"

#include <string.h>

/* ASCII codes the editor reacts to. Bytes are compared as unsigned char:
 * plain char is signed on x86/macOS hosts but unsigned on Arm, and the
 * behaviour must not depend on which. */
#define ASCII_CTRL_C          0x03U
#define ASCII_BACKSPACE       0x08U
#define ASCII_LF              0x0AU
#define ASCII_CR              0x0DU
#define ASCII_FIRST_PRINTABLE 0x20U /* space */
#define ASCII_LAST_PRINTABLE  0x7EU /* '~' */
#define ASCII_DELETE          0x7FU

void line_editor_init(line_editor_t *le)
{
    (void)memset(le, 0, sizeof(*le));
}

line_event_t line_editor_feed(line_editor_t *le, char c)
{
    unsigned char uc = (unsigned char)c;

    /* A completed line is consumed by the time the next byte arrives. */
    if (le->ready) {
        le->ready = false;
        le->len = 0U;
    }

    bool after_cr = le->last_was_cr;
    le->last_was_cr = (uc == ASCII_CR);

    if ((uc == ASCII_CR) || (uc == ASCII_LF)) {
        if ((uc == ASCII_LF) && after_cr) {
            return LE_NONE; /* second half of CR LF */
        }
        if (le->overflowed) {
            le->overflowed = false;
            le->len = 0U;
            return LE_TOO_LONG;
        }
        le->buf[le->len] = '\0';
        le->ready = true;
        return LE_LINE_READY;
    }
    if (uc == ASCII_CTRL_C) {
        le->len = 0U;
        le->overflowed = false;
        return LE_CANCEL;
    }
    if ((uc == ASCII_BACKSPACE) || (uc == ASCII_DELETE)) {
        if (le->len == 0U) {
            return LE_NONE;
        }
        le->len--;
        return LE_ERASE;
    }
    if ((uc < ASCII_FIRST_PRINTABLE) || (uc > ASCII_LAST_PRINTABLE)) {
        return LE_NONE; /* other control or non-ASCII bytes */
    }
    if (le->len >= LINE_EDITOR_MAX) {
        le->overflowed = true;
        return LE_OVERFLOW;
    }
    le->buf[le->len] = c;
    le->len++;
    return LE_ECHO;
}

const char *line_editor_line(const line_editor_t *le)
{
    return le->buf;
}
