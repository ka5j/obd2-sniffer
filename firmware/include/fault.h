/*
 * Crash capture and reporting.
 *
 * When the CPU hits a fault (bad memory access, undefined instruction,
 * divide by zero, ...), the fault handler saves the CPU registers and the
 * fault status registers into RAM that survives a reset, then restarts the
 * chip. On the next start-up fault_init() picks the record up, so the crash
 * can be reported (start-up report, `fault` command) even though the device
 * kept running.
 *
 * The record format, its validation and the decoding of the status
 * registers are in the portable library (lib/fault_record) and unit-tested.
 */
#ifndef FAULT_H
#define FAULT_H

#include "fault_record.h"

/* Enable the separate MemManage, BusFault and UsageFault handlers and the
 * divide-by-zero trap, and collect the record left by a crash, if any.
 * Call early in start-up. */
void fault_init(void);

/* Crash record from before the last reset, or NULL if there was none. */
const fault_record_t *fault_last(void);

/* Forget the last crash record. */
void fault_clear(void);

/* Deliberate faults for testing the capture path (debug builds only). */
typedef enum {
    FAULT_TEST_BUS,      /* read from an unmapped address: BusFault */
    FAULT_TEST_USAGE,    /* unaligned word read with the trap enabled: UsageFault */
    FAULT_TEST_DIVZERO,  /* integer divide by zero: UsageFault */
    FAULT_TEST_HARD,     /* bus error with BusFault disabled: escalates to HardFault */
    FAULT_TEST_WATCHDOG, /* stop the main loop: the watchdog resets the chip */
} fault_test_t;

void fault_trigger(fault_test_t kind);

#endif /* FAULT_H */
