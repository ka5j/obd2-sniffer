/*
 * Collects the bytes the laptop sends into command lines.
 *
 * A line ends on CR or LF; an LF directly after a CR is ignored, so CR LF
 * counts once. Ctrl-C discards whatever has arrived of the current line,
 * which lets the laptop start from a clean line after a broken connection.
 * Other control bytes and non-ASCII bytes are ignored.
 *
 * A line longer than LINE_READER_MAX is rejected as a whole when it ends,
 * never passed on cut short: a truncated command could do something other
 * than what was sent.
 */
#ifndef LINE_READER_H
#define LINE_READER_H

#include <stdbool.h>
#include <stddef.h>

#define LINE_READER_MAX 96U /* characters per line, excluding the terminator */

typedef enum {
    LR_NONE,       /* nothing to do yet */
    LR_LINE_READY, /* a complete line is available via line_reader_line() */
    LR_TOO_LONG,   /* a line that overflowed has ended: reject it */
} line_event_t;

typedef struct {
    char buf[LINE_READER_MAX + 1U];
    size_t len;
    bool last_was_cr;
    bool ready;
    bool overflowed; /* bytes were dropped from the current line */
} line_reader_t;

void line_reader_init(line_reader_t *lr);

/* Feed one received byte. */
line_event_t line_reader_feed(line_reader_t *lr, char c);

/* After LR_LINE_READY: the NUL-terminated line. Valid until the next feed.
 * Copy it before parsing in place (the command parser modifies its input). */
const char *line_reader_line(const line_reader_t *lr);

#endif /* LINE_READER_H */
