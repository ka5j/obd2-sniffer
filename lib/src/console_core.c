#include "console_core.h"

#include <string.h>

#define HELP_USAGE_WIDTH 24U /* column where the help text starts */

static void out(console_core_t *con, const char *text)
{
    con->write(con->user, text, strlen(text));
}

static void out_spaces(console_core_t *con, size_t count)
{
    for (size_t i = 0U; i < count; i++) {
        con->write(con->user, " ", 1U);
    }
}

/* Pad from `used` columns to `width`, always leaving at least one space. */
static void out_pad(console_core_t *con, size_t used, size_t width)
{
    out_spaces(con, (used < width) ? (width - used) : 1U);
}

static bool interactive(const console_core_t *con)
{
    return con->mode == CONSOLE_MODE_INTERACTIVE;
}

void console_core_init(console_core_t *con, const cli_command_t *commands, size_t command_count,
                       console_write_fn write, void *user)
{
    (void)memset(con, 0, sizeof(*con));
    line_editor_init(&con->editor);
    con->mode = CONSOLE_MODE_INTERACTIVE;
    con->commands = commands;
    con->command_count = command_count;
    con->write = write;
    con->user = user;
}

console_mode_t console_core_mode(const console_core_t *con)
{
    return con->mode;
}

void console_core_set_mode(console_core_t *con, console_mode_t mode)
{
    con->mode = mode;
}

void console_core_prompt(console_core_t *con)
{
    if (interactive(con)) {
        out(con, CONSOLE_PROMPT);
    }
}

void console_core_text(console_core_t *con, const char *text)
{
    out(con, text);
}

void console_core_field(console_core_t *con, const char *key, const char *label, const char *value)
{
    if (interactive(con)) {
        out(con, "  ");
        out(con, label);
        out_pad(con, strlen(label), CONSOLE_LABEL_WIDTH);
    } else {
        out(con, key);
        out(con, "=");
    }
    out(con, value);
    out(con, "\r\n");
}

void console_core_event(console_core_t *con, const char *text)
{
    if (!interactive(con)) {
        out(con, "!");
        out(con, text);
        out(con, "\r\n");
        return;
    }
    if (con->executing) {
        /* Mid-command: the command's own output is in progress. */
        out(con, text);
        out(con, "\r\n");
        return;
    }
    /* Print the event on its own line, then restore the prompt and whatever
     * the user had typed so far. */
    out(con, "\r\n");
    out(con, text);
    out(con, "\r\n");
    out(con, CONSOLE_PROMPT);
    if (!con->editor.ready) {
        con->write(con->user, con->editor.buf, con->editor.len);
    }
}

cli_status_t console_core_help(console_core_t *con, const char *name)
{
    if (name != NULL) {
        const cli_command_t *cmd = cli_find(con->commands, con->command_count, name);
        if (cmd == NULL) {
            return CLI_ERR_INVALID;
        }
        out(con, "usage: ");
        out(con, cmd->name);
        out(con, " ");
        out(con, cmd->usage);
        out(con, "\r\n  ");
        out(con, cmd->help);
        out(con, "\r\n");
        return CLI_OK;
    }

    for (size_t i = 0U; i < con->command_count; i++) {
        const cli_command_t *cmd = &con->commands[i];
        out(con, "  ");
        out(con, cmd->name);
        out(con, " ");
        out(con, cmd->usage);
        out_pad(con, strlen(cmd->name) + 1U + strlen(cmd->usage), HELP_USAGE_WIDTH);
        out(con, cmd->help);
        out(con, "\r\n");
    }
    return CLI_OK;
}

/* Report how a command line ended, in the style of the current mode (which
 * the command itself may just have changed). */
static void report(console_core_t *con, cli_status_t status, const cli_command_t *cmd)
{
    if (status == CLI_ERR_EMPTY) {
        console_core_prompt(con);
        return;
    }
    if (status == CLI_OK) {
        if (!interactive(con)) {
            out(con, "OK\r\n");
        }
        console_core_prompt(con);
        return;
    }

    out(con, interactive(con) ? "error: " : "ERR ");
    out(con, cli_status_str(status));
    if (status == CLI_ERR_ARGS) {
        /* cli_execute() only reports CLI_ERR_ARGS for a command it found. */
        out(con, " (usage: ");
        out(con, cmd->name);
        out(con, " ");
        out(con, cmd->usage);
        out(con, ")");
    }
    if ((status == CLI_ERR_UNKNOWN) && interactive(con)) {
        out(con, " (type 'help' for a list)");
    }
    out(con, "\r\n");
    console_core_prompt(con);
}

static void run_line(console_core_t *con)
{
    /* The parser splits the line in place; work on a copy so the editor's
     * buffer is left intact. */
    const char *line = line_editor_line(&con->editor);
    (void)memcpy(con->exec_line, line, strlen(line) + 1U);

    const cli_command_t *cmd = NULL;
    con->executing = true;
    cli_status_t status = cli_execute(con->commands, con->command_count, con->exec_line, con, &cmd);
    con->executing = false;
    report(con, status, cmd);
}

void console_core_feed(console_core_t *con, char byte)
{
    line_event_t event = line_editor_feed(&con->editor, byte);
    bool echo = interactive(con);

    switch (event) {
    case LE_ECHO:
        if (echo) {
            con->write(con->user, &byte, 1U);
        }
        break;
    case LE_ERASE:
        if (echo) {
            out(con, "\b \b");
        }
        break;
    case LE_CANCEL:
        if (echo) {
            out(con, "^C\r\n");
            out(con, CONSOLE_PROMPT);
        }
        break;
    case LE_OVERFLOW:
        if (echo) {
            out(con, "\a"); /* terminal bell: line is full */
        }
        break;
    case LE_LINE_READY:
        if (echo) {
            out(con, "\r\n");
        }
        run_line(con);
        break;
    default:
        /* LE_NONE: the byte needs no response (e.g. the LF of a CR LF). */
        break;
    }
}
