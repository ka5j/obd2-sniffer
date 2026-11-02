#include "console/console_core.h"

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

void console_core_init(console_core_t *con, const cli_command_t *commands, size_t command_count,
                       console_write_fn write, void *user)
{
    (void)memset(con, 0, sizeof(*con));
    line_reader_init(&con->reader);
    con->commands = commands;
    con->command_count = command_count;
    con->write = write;
    con->user = user;
}

void console_core_text(console_core_t *con, const char *text)
{
    out(con, text);
}

void console_core_field(console_core_t *con, const char *key, const char *value)
{
    out(con, key);
    out(con, "=");
    out(con, value);
    out(con, "\r\n");
}

void console_core_event(console_core_t *con, const char *text)
{
    out(con, "!");
    out(con, text);
    out(con, "\r\n");
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
        size_t used = strlen(cmd->name) + 1U + strlen(cmd->usage);
        out(con, "  ");
        out(con, cmd->name);
        out(con, " ");
        out(con, cmd->usage);
        if (used < HELP_USAGE_WIDTH) {
            out_spaces(con, HELP_USAGE_WIDTH - used);
        } else {
            /* Too wide for the column: the text goes below, still aligned. */
            out(con, "\r\n  ");
            out_spaces(con, HELP_USAGE_WIDTH);
        }
        out(con, cmd->help);
        out(con, "\r\n");
    }
    return CLI_OK;
}

/* Report how a command line ended. An empty line gets no reply. */
static void report(console_core_t *con, cli_status_t status, const cli_command_t *cmd)
{
    if (status == CLI_ERR_EMPTY) {
        return;
    }
    if (status == CLI_OK) {
        out(con, "OK\r\n");
        return;
    }
    out(con, "ERR ");
    out(con, cli_status_str(status));
    if (status == CLI_ERR_ARGS) {
        /* cli_execute() only reports CLI_ERR_ARGS for a command it found. */
        out(con, " (usage: ");
        out(con, cmd->name);
        out(con, " ");
        out(con, cmd->usage);
        out(con, ")");
    }
    out(con, "\r\n");
}

static void run_line(console_core_t *con)
{
    /* The parser splits the line in place; work on a copy so the reader's
     * buffer is left intact. */
    const char *line = line_reader_line(&con->reader);
    (void)memcpy(con->exec_line, line, strlen(line) + 1U);

    const cli_command_t *cmd = NULL;
    cli_status_t status = cli_execute(con->commands, con->command_count, con->exec_line, con, &cmd);
    report(con, status, cmd);
}

bool console_core_feed(console_core_t *con, char byte)
{
    line_event_t event = line_reader_feed(&con->reader, byte);
    if (event == LR_LINE_READY) {
        run_line(con);
    } else if (event == LR_TOO_LONG) {
        /* Rejected whole: running it cut short could do something else. */
        report(con, CLI_ERR_TOO_LONG, NULL);
    } else {
        /* LR_NONE: the line isn't complete yet. */
    }
    return event != LR_NONE;
}
