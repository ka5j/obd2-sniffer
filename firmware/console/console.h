/*
 * The board's serial console: the portable console core (lib/console/console_core)
 * connected to the serial link (uart_link).
 *
 * Call console_poll() from the main loop to process received input. Output
 * functions queue text for the DMA-driven transmitter and return straight
 * away; they must only be used from the main loop.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>
#include <stddef.h>

/* Free transmit space needed before the next command starts: more than the
 * longest reply, so a reply never has to wait for space or be cut short.
 * Other output (such as streamed CAN frames) leaves this much free. Checked
 * against the simulator's replies by host/tests/test_matches_firmware.py. */
#define CONSOLE_REPLY_SPACE 1024U

bool console_init(void);

/* Process received bytes: echo, editing, running commands. Runs at most one
 * command per call, so the main loop stays responsive. */
void console_poll(void);

void console_write(const char *data, size_t len);
void console_puts(const char *text);

/* printf-style output through a fixed stack buffer (no heap). Output longer
 * than the buffer is truncated. */
void console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* For command handlers: one data item formatted for the current mode
 * ("key=value" in host mode, aligned "label  value" interactively). `ctx`
 * is the handler's context argument. */
void console_fieldf(void *ctx, const char *key, const char *label, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* Show the prompt (interactive mode only). */
void console_prompt(void);

/* Show the prompt again with whatever has been typed so far (interactive
 * mode only), after other output interrupted the input line. */
void console_redraw_prompt(void);

bool console_is_interactive(void);

/* Report something that happened outside a command, e.g. a button press. */
void console_event(const char *text);

#endif /* CONSOLE_H */
