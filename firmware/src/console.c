#include "console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "commands.h"
#include "console_core.h"
#include "uart_link.h"

#define CONSOLE_FORMAT_BUF    160U
#define CONSOLE_TX_TIMEOUT_MS 50U /* longest wait for transmit buffer space */

static console_core_t core;

static void write_out(void *user, const char *data, size_t len)
{
    (void)user;
    (void)uart_link_write((const uint8_t *)data, len, CONSOLE_TX_TIMEOUT_MS);
}

bool console_init(void)
{
    if (!uart_link_init()) {
        return false;
    }
    console_core_init(&core, commands_table(), commands_count(), write_out, NULL);
    return true;
}

void console_poll(void)
{
    uint8_t byte = 0U;
    while (uart_link_read(&byte)) {
        console_core_feed(&core, (char)byte);
    }
}

void console_write(const char *data, size_t len)
{
    write_out(NULL, data, len);
}

void console_puts(const char *text)
{
    console_write(text, strlen(text));
}

void console_prompt(void)
{
    console_core_prompt(&core);
}

void console_event(const char *text)
{
    console_core_event(&core, text);
}

/* The only uses of <stdarg.h> and <stdio.h> in the firmware: deviation
 * DEV-01 in misra/config.toml. */
void console_printf(const char *fmt, ...)
{
    char buf[CONSOLE_FORMAT_BUF];
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

void console_fieldf(void *ctx, const char *key, const char *label, const char *fmt, ...)
{
    char value[CONSOLE_FORMAT_BUF];
    va_list args; /* MISRA DEV-01 */

    va_start(args, fmt);                                /* MISRA DEV-01 */
    int n = vsnprintf(value, sizeof(value), fmt, args); /* MISRA DEV-01 */
    va_end(args);                                       /* MISRA DEV-01 */

    if (n < 0) {
        value[0] = '\0';
    }
    console_core_field(ctx, key, label, value);
}
