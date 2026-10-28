#include "system/status_led.h"

#include <stdbool.h>

#include "system/board.h"

#define HEARTBEAT_HALF_PERIOD_MS 500U

static status_led_mode_t current = STATUS_LED_AUTO;
static bool lit;

void status_led_set_mode(status_led_mode_t mode)
{
    current = mode;
    if (mode != STATUS_LED_AUTO) {
        lit = (mode == STATUS_LED_ON);
        board_led_set(lit);
    }
}

status_led_mode_t status_led_mode(void)
{
    return current;
}

const char *status_led_mode_name(status_led_mode_t mode)
{
    switch (mode) {
    case STATUS_LED_ON:
        return "on";
    case STATUS_LED_OFF:
        return "off";
    case STATUS_LED_AUTO:
    default:
        return "auto";
    }
}

void status_led_task(uint32_t now_ms)
{
    static uint32_t last_toggle_ms;

    if (current != STATUS_LED_AUTO) {
        return;
    }
    /* Non-blocking: compare elapsed time instead of delaying. Unsigned
     * subtraction stays correct when the millisecond counter wraps. */
    if ((now_ms - last_toggle_ms) >= HEARTBEAT_HALF_PERIOD_MS) {
        last_toggle_ms = now_ms;
        lit = !lit;
        board_led_set(lit);
    }
}
