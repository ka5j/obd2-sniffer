/*
 * Independent watchdog (IWDG).
 *
 * A hardware timer, clocked from the chip's internal low-speed oscillator,
 * that resets the chip unless the main loop refreshes it in time. If the
 * firmware hangs (an endless loop, a deadlock, interrupts left disabled),
 * the watchdog restarts it, and the next start-up reports "independent
 * watchdog" as the reset cause.
 *
 * Once started it cannot be stopped except by a reset. It is paused while a
 * debugger has the CPU halted, so stepping through code does not reset the
 * board.
 */
#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

/* Nominal timeout. The low-speed oscillator is only accurate to roughly
 * 17-47 kHz (datasheet), so the real timeout is 340-940 ms; the main loop
 * refreshes far more often than that. */
#define WATCHDOG_TIMEOUT_MS 500U

bool watchdog_init(void);

/* Call once per pass of the main loop. */
void watchdog_kick(void);

#endif /* WATCHDOG_H */
