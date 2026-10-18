#include "commands.h"

#include "console.h"
#include "console_core.h"
#include "status_led.h"
#include "stm32f4xx_hal.h"
#include "uart_link.h"
#include "version.h"

#ifdef NDEBUG
#define BUILD_TYPE "release"
#else
#define BUILD_TYPE "debug"
#endif

static reset_cause_t boot_reset_cause = RESET_CAUSE_UNKNOWN;
static clock_source_t boot_clock = CLOCK_SRC_HSE;
static bool reset_requested;

void commands_set_boot_info(reset_cause_t cause, clock_source_t clock)
{
    boot_reset_cause = cause;
    boot_clock = clock;
}

bool commands_reset_requested(void)
{
    return reset_requested;
}

/* ---- handlers ------------------------------------------------------------ */

static cli_status_t cmd_help(int argc, char *argv[], void *ctx)
{
    return console_core_help(ctx, (argc > 1) ? argv[1] : NULL);
}

static cli_status_t cmd_version(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)argv;
    console_fieldf(ctx, "firmware", "Firmware", "%s", FW_VERSION);
    console_fieldf(ctx, "build", "Build", "%s", BUILD_TYPE);
    console_fieldf(ctx, "mcu", "MCU", "STM32F446RE");
    console_fieldf(ctx, "revision", "Silicon rev", "0x%04lX", (unsigned long)HAL_GetREVID());
    console_fieldf(ctx, "uid", "Unique ID", "%08lX-%08lX-%08lX", (unsigned long)HAL_GetUIDw2(),
                   (unsigned long)HAL_GetUIDw1(), (unsigned long)HAL_GetUIDw0());
    console_fieldf(ctx, "compiler", "Compiler", "GCC %s", __VERSION__);
    return CLI_OK;
}

static cli_status_t cmd_status(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)argv;
    uart_link_stats_t link;
    uart_link_get_stats(&link);

    console_fieldf(ctx, "uptime_ms", "Uptime (ms)", "%lu", (unsigned long)HAL_GetTick());
    console_fieldf(ctx, "reset_cause", "Last reset", "%s", reset_cause_name(boot_reset_cause));
    console_fieldf(ctx, "clock", "Clock source", "%s", clock_source_name(boot_clock));
    console_fieldf(ctx, "sysclk_hz", "CPU clock (Hz)", "%lu",
                   (unsigned long)HAL_RCC_GetSysClockFreq());
    console_fieldf(ctx, "led", "LED", "%s", status_led_mode_name(status_led_mode()));
    console_fieldf(ctx, "uart_rx_bytes", "Serial received", "%lu", (unsigned long)link.rx_bytes);
    console_fieldf(ctx, "uart_rx_dropped", "  dropped", "%lu", (unsigned long)link.rx_dropped);
    console_fieldf(ctx, "uart_rx_overruns", "  overruns", "%lu", (unsigned long)link.rx_overruns);
    console_fieldf(ctx, "uart_rx_errors", "  errors", "%lu", (unsigned long)link.rx_errors);
    console_fieldf(ctx, "uart_tx_bytes", "Serial sent", "%lu", (unsigned long)link.tx_bytes);
    console_fieldf(ctx, "uart_tx_dropped", "  dropped", "%lu", (unsigned long)link.tx_dropped);
    console_fieldf(ctx, "uart_tx_dma_errors", "  DMA errors", "%lu",
                   (unsigned long)link.tx_dma_errors);
    return CLI_OK;
}

static cli_status_t cmd_led(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)ctx;
    if (cli_streq(argv[1], "on")) {
        status_led_set_mode(STATUS_LED_ON);
    } else if (cli_streq(argv[1], "off")) {
        status_led_set_mode(STATUS_LED_OFF);
    } else if (cli_streq(argv[1], "auto")) {
        status_led_set_mode(STATUS_LED_AUTO);
    } else {
        return CLI_ERR_INVALID;
    }
    return CLI_OK;
}

static cli_status_t cmd_mode(int argc, char *argv[], void *ctx)
{
    (void)argc;
    if (cli_streq(argv[1], "host")) {
        console_core_set_mode(ctx, CONSOLE_MODE_HOST);
    } else if (cli_streq(argv[1], "interactive")) {
        console_core_set_mode(ctx, CONSOLE_MODE_INTERACTIVE);
    } else {
        return CLI_ERR_INVALID;
    }
    return CLI_OK;
}

static cli_status_t cmd_reset(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    reset_requested = true;
    return CLI_OK;
}

/* ---- table --------------------------------------------------------------- */

static const cli_command_t table[] = {
    {"help", "[command]", "List commands, or show how to use one", 0U, 1U, cmd_help},
    {"version", "", "Firmware version and chip identity", 0U, 0U, cmd_version},
    {"status", "", "Uptime, last reset, clocks and serial statistics", 0U, 0U, cmd_status},
    {"led", "on|off|auto", "Green LED: on, off, or heartbeat (auto)", 1U, 1U, cmd_led},
    {"mode", "interactive|host", "Console mode (host is for the laptop tool)", 1U, 1U, cmd_mode},
    {"reset", "", "Restart the board", 0U, 0U, cmd_reset},
};

const cli_command_t *commands_table(void)
{
    return table;
}

size_t commands_count(void)
{
    return sizeof(table) / sizeof(table[0]);
}
