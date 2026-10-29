/*
 * HAL configuration for the OBD2 sniffer.
 *
 * Trimmed from ST's stm32f4xx_hal_conf_template.h: only the modules this
 * project uses are enabled, so unused drivers are neither compiled nor linked.
 */
#ifndef STM32F4XX_HAL_CONF_H
#define STM32F4XX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Enabled HAL modules ------------------------------------------------ */
#define HAL_MODULE_ENABLED
#define HAL_CAN_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_RTC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* ---- Oscillator values ---------------------------------------------------
 * On the Nucleo-64 the HSE input is fed by the 8 MHz MCO output of the
 * on-board ST-LINK (HSE bypass mode). HSE_VALUE is also passed on the compiler
 * command line so system_stm32f4xx.c agrees with the HAL. */
#if !defined(HSE_VALUE)
#define HSE_VALUE 8000000U
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT 100U /* ms */
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE 16000000U
#endif
#if !defined(LSI_VALUE)
#define LSI_VALUE 32000U
#endif
#if !defined(LSE_VALUE)
#define LSE_VALUE 32768U
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT 5000U /* ms */
#endif
#if !defined(EXTERNAL_CLOCK_VALUE)
#define EXTERNAL_CLOCK_VALUE 12288000U /* I2S_CKIN, unused */
#endif

/* ---- System configuration ----------------------------------------------- */
#define VDD_VALUE                3300U /* mV */
#define TICK_INT_PRIORITY        0x0FU /* SysTick at the lowest priority */
#define USE_RTOS                 0U
#define PREFETCH_ENABLE          1U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE        1U
#define USE_SPI_CRC              0U

/* Weak-symbol callbacks (HAL_CAN_RxFifo0MsgPendingCallback etc.) are used
 * instead of runtime-registered function pointers. */
#define USE_HAL_CAN_REGISTER_CALLBACKS  0U
#define USE_HAL_RTC_REGISTER_CALLBACKS  0U
#define USE_HAL_TIM_REGISTER_CALLBACKS  0U
#define USE_HAL_UART_REGISTER_CALLBACKS 0U

/* ---- Module headers ----------------------------------------------------- */
#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f4xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32f4xx_hal_gpio.h"
#endif
#ifdef HAL_EXTI_MODULE_ENABLED
#include "stm32f4xx_hal_exti.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32f4xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f4xx_hal_cortex.h"
#endif
#ifdef HAL_CAN_MODULE_ENABLED
#include "stm32f4xx_hal_can.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32f4xx_hal_flash.h"
#endif
#ifdef HAL_IWDG_MODULE_ENABLED
#include "stm32f4xx_hal_iwdg.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32f4xx_hal_pwr.h"
#endif
#ifdef HAL_RTC_MODULE_ENABLED
#include "stm32f4xx_hal_rtc.h"
#endif
#ifdef HAL_TIM_MODULE_ENABLED
#include "stm32f4xx_hal_tim.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
#include "stm32f4xx_hal_uart.h"
#endif

/* ---- Parameter checking ------------------------------------------------- */
#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32F4XX_HAL_CONF_H */
