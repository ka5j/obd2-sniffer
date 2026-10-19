/*
 * Microsecond time and CPU cycle counts.
 *
 * TIM2, a 32-bit timer, counts microseconds from start-up. Every received
 * CAN frame is stamped with it. It wraps after 2^32 us (71.6 minutes); the
 * laptop tool tracks the wraps.
 *
 * The DWT cycle counter (one count per CPU clock, 5.6 ns at 180 MHz)
 * measures how long code takes, such as the CAN receive interrupt.
 */
#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdbool.h>
#include <stdint.h>

/* Start both counters. Call after clock_init(). False if the timer clock is
 * not a whole number of MHz. */
bool timebase_init(void);

uint32_t timebase_us(void);
uint32_t timebase_cycles(void);

/* CPU cycles to microseconds, rounded down. */
uint32_t timebase_cycles_to_us(uint32_t cycles);

#endif /* TIMEBASE_H */
