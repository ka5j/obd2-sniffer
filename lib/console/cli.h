/*
 * Command-line parsing and dispatch.
 *
 * A command line such as "led on" is split into words (argv), the first word
 * is looked up in a table of commands, the argument count is checked, and the
 * command's handler is called. Matching is case-insensitive.
 *
 * The table and handlers are supplied by the application; this module only
 * does the parsing and lookup, so it can be unit-tested without hardware.
 */
#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLI_MAX_ARGS 8U /* including the command name */

typedef enum {
    CLI_OK = 0,
    CLI_ERR_EMPTY,    /* blank line: nothing to do */
    CLI_ERR_UNKNOWN,  /* no such command */
    CLI_ERR_ARGS,     /* wrong number of arguments */
    CLI_ERR_TOO_MANY, /* more words than CLI_MAX_ARGS */
    CLI_ERR_INVALID,  /* handler rejected an argument value */
    CLI_ERR_FAILED,   /* handler could not carry out the command */
    CLI_ERR_TOO_LONG, /* line longer than the console accepts (reported by the console) */
} cli_status_t;

typedef cli_status_t (*cli_handler_t)(int argc, char *argv[], void *ctx);

typedef struct {
    const char *name;
    const char *usage; /* arguments, e.g. "on|off|auto"; "" if none */
    const char *help;  /* one-line description */
    uint8_t min_args;  /* arguments after the command name */
    uint8_t max_args;
    cli_handler_t handler;
} cli_command_t;

/* Split `line` in place at spaces/tabs. Returns the number of words, or -1
 * if there are more than `max` words. */
int cli_tokenize(char *line, char *argv[], size_t max);

/* Case-insensitive lookup. Returns NULL if not found. */
const cli_command_t *cli_find(const cli_command_t *table, size_t count, const char *name);

/* Tokenize, look up and run one command line (modified in place). If `found`
 * is not NULL it receives the matched command, or NULL. */
cli_status_t cli_execute(const cli_command_t *table, size_t count, char *line, void *ctx,
                         const cli_command_t **found);

/* Short human-readable name of a status, e.g. "unknown command". */
const char *cli_status_str(cli_status_t status);

/* Parse an unsigned 32-bit number: decimal ("2024") or hex ("0x7DF").
 * Rejects empty strings, signs, stray characters and overflow. */
bool cli_parse_u32(const char *s, uint32_t *out);

/* Parse 1-8 hex digits with no prefix, e.g. "7DF" or "18daf110", the way CAN
 * identifiers are usually written. */
bool cli_parse_hex_u32(const char *s, uint32_t *out);

/* Parse pairs of hex digits as bytes, e.g. "0201050000" gives 02 01 05 00 00.
 * False if empty, odd in length, not hex, or more than `max` bytes. */
bool cli_parse_hex_bytes(const char *s, uint8_t *out, size_t max, size_t *len);

/* Case-insensitive string equality (ASCII). */
bool cli_streq(const char *a, const char *b);

#endif /* CLI_H */
