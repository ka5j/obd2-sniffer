/*
 * Board support for the Nucleo-F446RE (MB1136).
 *
 * Pin assignments for the whole project live here so there is one place to
 * check against the Nucleo user manual (UM1724) and the wiring docs.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>

#include "stm32f4xx_hal.h"

/* LD2, green user LED (active high). Arduino D13. */
#define BOARD_LED_PORT GPIOA
#define BOARD_LED_PIN  GPIO_PIN_5

/* B1, blue user button. Active low, external pull-up on the board. */
#define BOARD_BTN_PORT GPIOC
#define BOARD_BTN_PIN  GPIO_PIN_13

/* USART2 is wired to the ST-LINK virtual COM port (SB13/SB14 default). */
#define BOARD_VCP_TX_PORT GPIOA
#define BOARD_VCP_TX_PIN  GPIO_PIN_2
#define BOARD_VCP_RX_PORT GPIOA
#define BOARD_VCP_RX_PIN  GPIO_PIN_3
#define BOARD_VCP_AF      GPIO_AF7_USART2

void board_init(void);

void board_led_set(bool on);
void board_led_toggle(void);

/* Raw level of B1, true while held down. Not debounced. */
bool board_button_pressed(void);

#endif /* BOARD_H */
