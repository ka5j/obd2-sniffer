/*
 * Console commands: the table the console runs, and the board state those
 * commands report.
 */
#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>
#include <stddef.h>

#include "cli.h"
#include "clock.h"
#include "reset_cause.h"

const cli_command_t *commands_table(void);
size_t commands_count(void);

/* Facts established at start-up, reported by `status`. */
void commands_set_boot_info(reset_cause_t cause, clock_source_t clock);

/* True once `reset` has been accepted. The main loop sends the reply, waits
 * for it to leave the UART, then restarts the chip. */
bool commands_reset_requested(void);

#endif /* COMMANDS_H */
