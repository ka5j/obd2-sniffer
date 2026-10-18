#include "console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board.h"

#define CONSOLE_PRINTF_BUF 160U
#define CONSOLE_TX_TIMEOUT 100U /* ms, per write */

static UART_HandleTypeDef huart2;

void console_init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = CONSOLE_BAUD;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX; /* MISRA DEV-02 */
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    /* If the UART can't be configured there is nowhere to report it; the
     * LED heartbeat still shows the firmware is alive. */
    (void)HAL_UART_Init(&huart2);
}

/* Signature fixed by the HAL, which declares it weak. */
// cppcheck-suppress constParameterPointer
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) {
        return;
    }

    __HAL_RCC_USART2_CLK_ENABLE(); /* MISRA DEV-02 */
    __HAL_RCC_GPIOA_CLK_ENABLE();  /* MISRA DEV-02 */

    GPIO_InitTypeDef pin = {
        .Pin = BOARD_VCP_TX_PIN,
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_PULLUP, /* idle-high line stays defined if ST-LINK is unpowered */
        .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = BOARD_VCP_AF,
    };
    HAL_GPIO_Init(BOARD_VCP_TX_PORT, &pin);

    pin.Pin = BOARD_VCP_RX_PIN;
    HAL_GPIO_Init(BOARD_VCP_RX_PORT, &pin);
}

void console_write(const char *data, size_t len)
{
    size_t offset = 0U;

    while (offset < len) {
        /* HAL takes a uint16_t length. */
        size_t remaining = len - offset;
        uint16_t chunk =
            (remaining > (size_t)UINT16_MAX) ? (uint16_t)UINT16_MAX : (uint16_t)remaining;
        (void)HAL_UART_Transmit(&huart2, (const uint8_t *)&data[offset], chunk, CONSOLE_TX_TIMEOUT);
        offset += chunk;
    }
}

void console_puts(const char *s)
{
    console_write(s, strlen(s));
}

/* The only use of <stdarg.h> and <stdio.h> in the firmware: see deviation
 * DEV-01 in misra/config.toml. */
void console_printf(const char *fmt, ...)
{
    char buf[CONSOLE_PRINTF_BUF];
    va_list args; /* MISRA DEV-01 */

    va_start(args, fmt);                            /* MISRA DEV-01 */
    int n = vsnprintf(buf, sizeof(buf), fmt, args); /* MISRA DEV-01 */
    va_end(args);                                   /* MISRA DEV-01 */

    if (n < 0) {
        return;
    }
    size_t len = ((size_t)n < sizeof(buf)) ? (size_t)n : (sizeof(buf) - 1U);
    console_write(buf, len);
}
