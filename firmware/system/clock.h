/*
 * System clock tree.
 *
 *   HSE 8 MHz (bypass, from ST-LINK MCO)  --/M=4-->  2 MHz  --xN=168-->  VCO 336 MHz
 *   VCO --/P=2--> SYSCLK 168 MHz
 *   VCO --/Q=7--> 48 MHz (USB)
 *   SYSCLK --/1--> HCLK 168 MHz --/4--> PCLK1 42 MHz (bxCAN, USART2, TIM2 x2 = 84 MHz)
 *                               --/2--> PCLK2 84 MHz
 *
 * 168 MHz is the fastest speed that also gives USB its exact 48 MHz, and it
 * needs no over-drive mode. There is no fallback to the internal 16 MHz
 * oscillator: it is trimmed to +-1% only at 25 C and drifts several percent
 * over temperature, more than CAN bit timing or USB tolerate. A board whose
 * external clock doesn't start stops in board_panic() rather than run with
 * timing it can't trust.
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>

#define CLOCK_SOURCE_NAME "HSE 8 MHz bypass" /* reported in the start-up report and `status` */

/* Configure the clock tree for 168 MHz. False if the external clock or the
 * PLL didn't start; the system is then still on the 16 MHz HSI reset clock. */
bool clock_init(void);

#endif /* CLOCK_H */
