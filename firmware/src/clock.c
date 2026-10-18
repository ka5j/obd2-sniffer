#include "clock.h"

#include "stm32f4xx_hal.h"

/* PLL input must be 1-2 MHz; 2 MHz minimises PLL jitter (RM0390 6.3.2). */
#define PLL_M_HSE 4U /* 8 MHz / 4 = 2 MHz */
#define PLL_M_HSI 8U /* 16 MHz / 8 = 2 MHz */
#define PLL_N     180U
#define PLL_Q     8U /* 45 MHz on the 48 MHz domain: unused (no USB/SDIO) */
#define PLL_R     2U /* unused (no I2S/SAI/SPDIF) */

static bool pll_start(clock_source_t source)
{
    RCC_OscInitTypeDef osc = {0};

    if (source == CLOCK_SRC_HSE) {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
        osc.HSEState = RCC_HSE_BYPASS;
        osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
        osc.PLL.PLLM = PLL_M_HSE;
    } else {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
        osc.PLL.PLLM = PLL_M_HSI;
    }
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLN = PLL_N;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = PLL_Q;
    osc.PLL.PLLR = PLL_R;

    return HAL_RCC_OscConfig(&osc) == HAL_OK;
}

bool clock_init(clock_source_t *source_out)
{
    /* 180 MHz requires voltage scale 1 plus over-drive mode (DS10693 6.3.1). */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    clock_source_t source = CLOCK_SRC_HSE;
    if (!pll_start(source)) {
        source = CLOCK_SRC_HSI;
        if (!pll_start(source)) {
            return false;
        }
    }

    if (HAL_PWREx_EnableOverDrive() != HAL_OK) {
        return false;
    }

    RCC_ClkInitTypeDef clk = {
        .ClockType =
            RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2,
        .SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK,
        .AHBCLKDivider = RCC_SYSCLK_DIV1,
        .APB1CLKDivider = RCC_HCLK_DIV4, /* APB1 max 45 MHz */
        .APB2CLKDivider = RCC_HCLK_DIV2, /* APB2 max 90 MHz */
    };
    /* 5 wait states at 180 MHz / 3.3 V (RM0390 table 5). HAL raises the
     * latency before switching to the faster clock. */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        return false;
    }

    if (source_out != NULL) {
        *source_out = source;
    }
    return true;
}

const char *clock_source_name(clock_source_t source)
{
    return (source == CLOCK_SRC_HSE) ? "HSE 8 MHz bypass" : "HSI 16 MHz (fallback)";
}
