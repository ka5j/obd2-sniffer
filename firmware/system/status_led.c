#include "system/status_led.h"

#include <stdbool.h>

#include "system/board.h"

#define HEARTBEAT_HALF_PERIOD_MS 500U

void status_led_task(uint32_t now_ms)
{
    static uint32_t last_toggle_ms;

    /* Non-blocking: compare elapsed time instead of delaying. Unsigned
     * subtraction stays correct when the millisecond counter wraps. */
    if ((now_ms - last_toggle_ms) >= HEARTBEAT_HALF_PERIOD_MS) {
        static bool lit;
        last_toggle_ms = now_ms;
        lit = !lit;
        board_led_set(lit);
    }
}
