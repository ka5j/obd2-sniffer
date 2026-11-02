/*
 * Green LED (LD2): a 1 Hz heartbeat that shows the main loop is running.
 */
#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdint.h>

/* Call from the main loop with the current time in milliseconds. */
void status_led_task(uint32_t now_ms);

#endif /* STATUS_LED_H */
