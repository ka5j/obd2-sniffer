/*
 * Text console on USART2, which the Nucleo routes to the ST-LINK virtual COM
 * port (shows up as /dev/tty.usbmodem* on macOS).
 *
 * Transmit is blocking (polled), which suits a boot banner and occasional
 * status messages. The interface hides the transport so it can move to
 * interrupt/DMA-driven I/O without touching callers.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stddef.h>

#define CONSOLE_BAUD 115200U

void console_init(void);

void console_write(const char *data, size_t len);
void console_puts(const char *s);

/* printf-style output through a fixed stack buffer: no heap, no stdio FILE.
 * Output longer than the buffer is truncated. */
void console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* CONSOLE_H */
