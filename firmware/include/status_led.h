/*
 * Green LED (LD2) behaviour: steady on, off, or a 1 Hz heartbeat that shows
 * the main loop is running.
 */
#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdint.h>

typedef enum {
    STATUS_LED_AUTO, /* heartbeat */
    STATUS_LED_ON,
    STATUS_LED_OFF,
} status_led_mode_t;

void status_led_set_mode(status_led_mode_t mode);
status_led_mode_t status_led_mode(void);
const char *status_led_mode_name(status_led_mode_t mode);

/* Call from the main loop with the current time in milliseconds. */
void status_led_task(uint32_t now_ms);

#endif /* STATUS_LED_H */
