#include "cli.h"

/* Distance between lowercase and uppercase ASCII letters. */
#define CASE_OFFSET ('a' - 'A')

static bool is_space(char c)
{
    return (c == ' ') || (c == '\t');
}

static char to_lower(char c)
{
    return ((c >= 'A') && (c <= 'Z')) ? (char)(c + CASE_OFFSET) : c;
}

bool cli_streq(const char *a, const char *b)
{
    size_t i = 0U;
    while ((a[i] != '\0') && (to_lower(a[i]) == to_lower(b[i]))) {
        i++;
    }
    return to_lower(a[i]) == to_lower(b[i]);
}

int cli_tokenize(char *line, char *argv[], size_t max)
{
    size_t argc = 0U;
    size_t i = 0U;

    for (;;) {
        while (is_space(line[i])) {
            line[i] = '\0';
            i++;
        }
        if (line[i] == '\0') {
            return (int)argc;
        }
        if (argc == max) {
            return -1;
        }
        argv[argc] = &line[i];
        argc++;
        while ((line[i] != '\0') && !is_space(line[i])) {
            i++;
        }
    }
}

const cli_command_t *cli_find(const cli_command_t *table, size_t count, const char *name)
{
    for (size_t i = 0U; i < count; i++) {
        if (cli_streq(table[i].name, name)) {
            return &table[i];
        }
    }
    return NULL;
}

cli_status_t cli_execute(const cli_command_t *table, size_t count, char *line, void *ctx,
                         const cli_command_t **found)
{
    char *argv[CLI_MAX_ARGS];
    const cli_command_t *cmd = NULL;
    cli_status_t status;

    int argc = cli_tokenize(line, argv, CLI_MAX_ARGS);
    if (argc < 0) {
        status = CLI_ERR_TOO_MANY;
    } else if (argc == 0) {
        status = CLI_ERR_EMPTY;
    } else {
        cmd = cli_find(table, count, argv[0]);
        int nargs = argc - 1;
        if (cmd == NULL) {
            status = CLI_ERR_UNKNOWN;
        } else if ((nargs < (int)cmd->min_args) || (nargs > (int)cmd->max_args)) {
            status = CLI_ERR_ARGS;
        } else {
            status = cmd->handler(argc, argv, ctx);
        }
    }

    if (found != NULL) {
        *found = cmd;
    }
    return status;
}

const char *cli_status_str(cli_status_t status)
{
    switch (status) {
    case CLI_OK:
        return "ok";
    case CLI_ERR_EMPTY:
        return "empty line";
    case CLI_ERR_UNKNOWN:
        return "unknown command";
    case CLI_ERR_ARGS:
        return "wrong number of arguments";
    case CLI_ERR_TOO_MANY:
        return "too many arguments";
    case CLI_ERR_INVALID:
        return "invalid argument";
    case CLI_ERR_FAILED:
        return "command failed";
    case CLI_ERR_TOO_LONG:
        return "line too long";
    default:
        return "unknown error";
    }
}

/* Value of one hex/decimal digit, or -1 if `c` is not a hex digit. */
static int digit_value(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    char l = to_lower(c);
    if ((l >= 'a') && (l <= 'f')) {
        return (l - 'a') + 10;
    }
    return -1;
}

bool cli_parse_u32(const char *s, uint32_t *out)
{
    uint32_t base = 10U;
    size_t i = 0U;

    if ((s[0] == '0') && (to_lower(s[1]) == 'x')) {
        base = 16U;
        i = 2U;
    }
    if (s[i] == '\0') {
        return false;
    }

    uint32_t value = 0U;
    for (; s[i] != '\0'; i++) {
        int d = digit_value(s[i]);
        if ((d < 0) || ((uint32_t)d >= base)) {
            return false;
        }
        if (value > ((UINT32_MAX - (uint32_t)d) / base)) {
            return false; /* would overflow */
        }
        value = (value * base) + (uint32_t)d;
    }
    *out = value;
    return true;
}
