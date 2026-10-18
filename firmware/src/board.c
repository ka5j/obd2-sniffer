#include "board.h"

void board_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN, GPIO_PIN_RESET);

    GPIO_InitTypeDef led = {
        .Pin = BOARD_LED_PIN,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init(BOARD_LED_PORT, &led);

    GPIO_InitTypeDef btn = {
        .Pin = BOARD_BTN_PIN,
        .Mode = GPIO_MODE_INPUT,
        .Pull = GPIO_NOPULL, /* R30 pull-up fitted on the Nucleo */
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init(BOARD_BTN_PORT, &btn);
}

void board_led_set(bool on)
{
    HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_led_toggle(void)
{
    HAL_GPIO_TogglePin(BOARD_LED_PORT, BOARD_LED_PIN);
}

bool board_button_pressed(void)
{
    return HAL_GPIO_ReadPin(BOARD_BTN_PORT, BOARD_BTN_PIN) == GPIO_PIN_RESET;
}
