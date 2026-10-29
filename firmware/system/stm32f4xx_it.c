/*
 * Cortex-M system exception handlers. The fault handlers (HardFault,
 * MemManage, BusFault, UsageFault) are in fault_entry.S, which hands over to
 * fault_capture() in fault.c; peripheral interrupt handlers live with their
 * drivers.
 */
#include "stm32f4xx_hal.h"

void NMI_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

void NMI_Handler(void)
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
