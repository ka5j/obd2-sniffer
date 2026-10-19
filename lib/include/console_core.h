/*
 * Serial console behaviour, independent of the hardware.
 *
 * Bytes received from the serial port are fed in one at a time. The console
 * collects them into lines, runs commands from a command table, and writes
 * all output through a callback, so the same logic runs on the board and in
 * the unit tests.
 *
 * Two modes:
 *
 *   Interactive (default): for a person in a terminal. Typed characters are
 *   echoed, backspace and Ctrl-C work, a prompt is shown, data is printed as
 *   aligned "label  value" rows, and errors are explained.
 *
 *   Host: for the laptop tool. No echo and no prompt. Data is printed as
 *   "key=value" lines and every command reply ends with a line that is
 *   exactly "OK" or starts with "ERR ". Unsolicited events start with "!".
 *
 * See docs/protocol.md for the full protocol.
 */
#ifndef CONSOLE_CORE_H
#define CONSOLE_CORE_H

#include <stdbool.h>
#include <stddef.h>

#include "cli.h"
#include "line_editor.h"

#define CONSOLE_PROMPT      "obd> "
#define CONSOLE_LABEL_WIDTH 18U /* column where interactive values start */

typedef enum {
    CONSOLE_MODE_INTERACTIVE,
    CONSOLE_MODE_HOST,
} console_mode_t;

/* Output callback: write `len` bytes (not NUL-terminated). */
typedef void (*console_write_fn)(void *user, const char *data, size_t len);

typedef struct {
    line_editor_t editor;
    console_mode_t mode;
    const cli_command_t *commands;
    size_t command_count;
    console_write_fn write;
    void *user;
    char exec_line[LINE_EDITOR_MAX + 1U]; /* copy of the line being executed */
    bool executing;                       /* a command is running */
} console_core_t;

/* Command handlers receive the console_core_t as their `ctx` argument and
 * use the output functions below. */
void console_core_init(console_core_t *con, const cli_command_t *commands, size_t command_count,
                       console_write_fn write, void *user);

/* Process one received byte (echo, editing, and running complete lines).
 * Returns true when the byte ended a line: its command has run (or the line
 * was rejected) and its reply is out. Callers use this to bound the work
 * done at a time. */
bool console_core_feed(console_core_t *con, char byte);

console_mode_t console_core_mode(const console_core_t *con);
void console_core_set_mode(console_core_t *con, console_mode_t mode);

/* Show the prompt (interactive mode only), e.g. after the start-up report. */
void console_core_prompt(console_core_t *con);

/* ---- Output for command handlers ---- */

/* Raw text, written as-is. */
void console_core_text(console_core_t *con, const char *text);

/* One data item. Host mode: "key=value". Interactive: label padded to
 * CONSOLE_LABEL_WIDTH, then the value. */
void console_core_field(console_core_t *con, const char *key, const char *label, const char *value);

/* An event not caused by a command (e.g. a button press). Host mode:
 * "!text". Interactive: printed on its own line; the prompt and any
 * partially typed line are shown again below it. */
void console_core_event(console_core_t *con, const char *text);

/* List all commands, or show usage for one. Returns CLI_ERR_INVALID if
 * `name` is not a known command. */
cli_status_t console_core_help(console_core_t *con, const char *name);

#endif /* CONSOLE_CORE_H */
