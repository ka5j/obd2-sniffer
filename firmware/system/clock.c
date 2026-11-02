#include "system/clock.h"

#include "stm32f4xx_hal.h"

/* PLL input must be 1-2 MHz; 2 MHz minimises PLL jitter (RM0390 6.3.2). */
#define PLL_M 4U   /* 8 MHz / 4 = 2 MHz */
#define PLL_N 168U /* VCO 336 MHz */
#define PLL_Q 7U   /* 336 / 7 = 48 MHz for USB */
#define PLL_R 2U   /* unused (no I2S/SAI/SPDIF) */

bool clock_init(void)
{
    /* Voltage scale 1 allows up to 168 MHz without over-drive (DS10693 6.3.1). */
    __HAL_RCC_PWR_CLK_ENABLE();                                    /* MISRA DEV-02 */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1); /* MISRA DEV-02 */

    RCC_OscInitTypeDef osc = {
        .OscillatorType = RCC_OSCILLATORTYPE_HSE,
        .HSEState = RCC_HSE_BYPASS,
        .PLL =
            {
                .PLLState = RCC_PLL_ON,
                .PLLSource = RCC_PLLSOURCE_HSE,
                .PLLM = PLL_M,
                .PLLN = PLL_N,
                .PLLP = RCC_PLLP_DIV2,
                .PLLQ = PLL_Q,
                .PLLR = PLL_R,
            },
    };
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return false; /* no external clock: HAL gives up after HSE_STARTUP_TIMEOUT */
    }

    RCC_ClkInitTypeDef clk = {
        .ClockType =
            RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2,
        .SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK,
        .AHBCLKDivider = RCC_SYSCLK_DIV1,
        .APB1CLKDivider = RCC_HCLK_DIV4, /* APB1 max 45 MHz */
        .APB2CLKDivider = RCC_HCLK_DIV2, /* APB2 max 90 MHz */
    };
    /* 5 wait states above 150 MHz at 3.3 V (RM0390 table 5). HAL raises the
     * latency before switching to the faster clock. */
    return HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) == HAL_OK;
}
