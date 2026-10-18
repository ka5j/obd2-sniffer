/*
 * OBD2 CAN sniffer: firmware entry point.
 */
#include "board.h"

int main(void)
{
    HAL_Init();
    board_init();

    uint32_t last_toggle = HAL_GetTick();

    for (;;) {
        /* Non-blocking 1 Hz heartbeat: the loop never stalls in HAL_Delay(),
         * so later phases can add work here without restructuring. */
        if (HAL_GetTick() - last_toggle >= 500U) {
            last_toggle += 500U;
            board_led_toggle();
        }
    }
}
