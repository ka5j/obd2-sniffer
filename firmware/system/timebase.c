#include "system/timebase.h"

#include "stm32f4xx_hal.h"

#define HZ_PER_MHZ 1000000U

bool timebase_init(void)
{
    /* Timers on APB1 run at twice the bus clock when the bus is divided
     * (RM0390 6.2): 2 x 45 MHz = 90 MHz here. */
    uint32_t timer_hz = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) {
        timer_hz *= 2U;
    }
    if ((timer_hz % HZ_PER_MHZ) != 0U) {
        return false;
    }

    __HAL_RCC_TIM2_CLK_ENABLE(); /* MISRA DEV-02 */
    TIM2->CR1 = 0U;
    TIM2->PSC = (timer_hz / HZ_PER_MHZ) - 1U; /* one count per microsecond */
    TIM2->ARR = 0xFFFFFFFFU;                  /* free-running over the full 32 bits */
    TIM2->CNT = 0U;
    TIM2->EGR = TIM_EGR_UG; /* load the prescaler now rather than at the first overflow */
    TIM2->CR1 = TIM_CR1_CEN;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; /* enables the DWT unit */
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return true;
}

uint32_t timebase_us(void)
{
    return TIM2->CNT;
}

uint32_t timebase_cycles(void)
{
    return DWT->CYCCNT;
}

uint32_t timebase_cycles_to_us(uint32_t cycles)
{
    return cycles / (SystemCoreClock / HZ_PER_MHZ);
}
