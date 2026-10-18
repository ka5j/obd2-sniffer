#include "line_editor.h"

#include <string.h>

#define CHAR_CTRL_C    '\x03'
#define CHAR_BACKSPACE '\x08'
#define CHAR_DELETE    '\x7f'

void line_editor_init(line_editor_t *le)
{
    memset(le, 0, sizeof *le);
}

line_event_t line_editor_feed(line_editor_t *le, char c)
{
    /* A completed line is consumed by the time the next byte arrives. */
    if (le->ready) {
        le->ready = false;
        le->len = 0U;
    }

    bool after_cr = le->last_was_cr;
    le->last_was_cr = (c == '\r');

    if (c == '\r' || c == '\n') {
        if (c == '\n' && after_cr) {
            return LE_NONE; /* second half of CR LF */
        }
        le->buf[le->len] = '\0';
        le->ready = true;
        return LE_LINE_READY;
    }
    if (c == CHAR_CTRL_C) {
        le->len = 0U;
        return LE_CANCEL;
    }
    if (c == CHAR_BACKSPACE || c == CHAR_DELETE) {
        if (le->len == 0U) {
            return LE_NONE;
        }
        le->len--;
        return LE_ERASE;
    }
    /* Compare as unsigned: plain char is signed on x86/macOS hosts but
     * unsigned on Arm, and the result must not depend on which. */
    unsigned char uc = (unsigned char)c;
    if (uc < (unsigned char)' ' || uc > (unsigned char)'~') {
        return LE_NONE; /* other control or non-ASCII bytes */
    }
    if (le->len >= LINE_EDITOR_MAX) {
        return LE_OVERFLOW;
    }
    le->buf[le->len++] = c;
    return LE_ECHO;
}

char *line_editor_line(line_editor_t *le)
{
    return le->buf;
}
