#include "watchdog.h"

#include "stm32f4xx_hal.h"

/* LSI 32 kHz / 64 = 500 Hz, i.e. one count every 2 ms. */
#define WATCHDOG_PRESCALER  IWDG_PRESCALER_64
#define WATCHDOG_TICK_HZ    500U
#define WATCHDOG_RELOAD     ((WATCHDOG_TIMEOUT_MS * WATCHDOG_TICK_HZ) / 1000U)
#define WATCHDOG_RELOAD_MAX 0x0FFFU /* 12-bit counter */

_Static_assert(WATCHDOG_RELOAD <= WATCHDOG_RELOAD_MAX, "watchdog timeout too long for prescaler");

static IWDG_HandleTypeDef hiwdg;

bool watchdog_init(void)
{
    /* Pause the watchdog while a debugger has halted the CPU. */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = WATCHDOG_PRESCALER;
    hiwdg.Init.Reload = WATCHDOG_RELOAD;
    return HAL_IWDG_Init(&hiwdg) == HAL_OK;
}

void watchdog_kick(void)
{
    (void)HAL_IWDG_Refresh(&hiwdg);
}
