/*
 * Cortex-M exception handlers and peripheral IRQ handlers.
 *
 * Fault handlers currently park the CPU in a loop so a debugger can inspect
 * the stacked registers. Phase 2 replaces this with fault capture that
 * survives a reset and is reported on the next boot.
 */
#include "stm32f4xx_hal.h"

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

void NMI_Handler(void)
{
    for (;;) {
    }
}

void HardFault_Handler(void)
{
    for (;;) {
    }
}

void MemManage_Handler(void)
{
    for (;;) {
    }
}

void BusFault_Handler(void)
{
    for (;;) {
    }
}

void UsageFault_Handler(void)
{
    for (;;) {
    }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/* 1 ms system tick that drives HAL_GetTick()/HAL_Delay(). */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
