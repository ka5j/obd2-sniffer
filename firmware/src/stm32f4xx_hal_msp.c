/*
 * MCU Support Package: low-level init the HAL calls back into (clocks, pins,
 * NVIC) when a peripheral is initialised.
 */
#include "stm32f4xx_hal.h"

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    /* All priority bits are preemption priority: no sub-priorities. */
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
}
