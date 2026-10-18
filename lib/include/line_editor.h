/*
 * Line editor for the interactive serial console.
 *
 * Bytes typed in a terminal arrive one at a time. The editor collects them
 * into a line, handles backspace and Ctrl-C, and tells the caller what to
 * echo back so the terminal shows what was typed. It does no I/O itself,
 * which keeps it testable on the host.
 *
 * Terminals send different line endings (CR, LF or CR LF). A line completes
 * on CR or LF; an LF directly after a CR is ignored so CR LF counts once.
 */
#ifndef LINE_EDITOR_H
#define LINE_EDITOR_H

#include <stdbool.h>
#include <stddef.h>

#define LINE_EDITOR_MAX 96U /* characters per line, excluding the terminator */

typedef enum {
    LE_NONE,       /* nothing to do (ignored byte) */
    LE_ECHO,       /* echo the byte that was just added */
    LE_ERASE,      /* erase one character on screen: "\b \b" */
    LE_LINE_READY, /* a complete line is available via line_editor_line() */
    LE_CANCEL,     /* Ctrl-C: line discarded */
    LE_OVERFLOW,   /* line too long: byte dropped, bell / warn the user */
} line_event_t;

typedef struct {
    char buf[LINE_EDITOR_MAX + 1U];
    size_t len;
    bool last_was_cr;
    bool ready;
} line_editor_t;

void line_editor_init(line_editor_t *le);

/* Feed one received byte and get what the caller should do about it. */
line_event_t line_editor_feed(line_editor_t *le, char c);

/* After LE_LINE_READY: the NUL-terminated line. Valid until the next feed. */
char *line_editor_line(line_editor_t *le);

#endif /* LINE_EDITOR_H */
