/*
 * System clock tree.
 *
 *   HSE 8 MHz (bypass, from ST-LINK MCO)  --/M=4-->  2 MHz  --xN=180-->  VCO 360 MHz
 *   VCO --/P=2--> SYSCLK 180 MHz
 *   SYSCLK --/1--> HCLK 180 MHz --/4--> PCLK1 45 MHz (bxCAN, USART2, TIM2 x2 = 90 MHz)
 *                               --/2--> PCLK2 90 MHz
 *
 * If HSE does not start (e.g. solder bridges changed, or a custom board with
 * no external clock) the PLL falls back to HSI 16 MHz with M=8, giving the
 * same 180 MHz tree. HSI is factory-trimmed to +-1% only at 25 C and drifts
 * several percent over temperature, more than CAN bit timing tolerates, so
 * the fallback is reported in the boot banner.
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>

typedef enum {
    CLOCK_SRC_HSE, /* PLL fed from HSE bypass (normal) */
    CLOCK_SRC_HSI, /* PLL fed from HSI (fallback) */
} clock_source_t;

/* Configure the clock tree for 180 MHz. Returns false only if neither
 * oscillator could drive the PLL, in which case the system is still running
 * from the 16 MHz HSI reset clock. */
bool clock_init(clock_source_t *source_out);

const char *clock_source_name(clock_source_t source);

#endif /* CLOCK_H */
