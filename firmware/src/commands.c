#include "commands.h"

#include "can_drv.h"
#include "can_gen.h"
#include "can_stream.h"
#include "can_timing.h"
#include "console.h"
#include "console_core.h"
#include "fault.h"
#include "status_led.h"
#include "stm32f4xx_hal.h"
#include "timebase.h"
#include "uart_link.h"
#include "version.h"
#include "watchdog.h"

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
    console_fieldf(ctx, "watchdog_timeout_ms", "Watchdog (ms)", "%lu",
                   (unsigned long)WATCHDOG_TIMEOUT_MS);
    console_fieldf(ctx, "last_fault", "Last crash", "%s",
                   (fault_last() != NULL) ? fault_type_str(fault_last()->type) : "none");
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

static cli_status_t cmd_fault(int argc, char *argv[], void *ctx)
{
    if (argc > 1) {
        if (!cli_streq(argv[1], "clear")) {
            return CLI_ERR_INVALID;
        }
        fault_clear();
        return CLI_OK;
    }

    const fault_record_t *rec = fault_last();
    if (rec == NULL) {
        console_fieldf(ctx, "fault", "Last crash", "none");
        return CLI_OK;
    }
    console_fieldf(ctx, "fault", "Last crash", "%s", fault_type_str(rec->type));
    console_fieldf(ctx, "cause", "Cause", "%s", fault_cause_str(rec->cfsr, rec->hfsr));
    console_fieldf(ctx, "pc", "At address (PC)", "0x%08lX", (unsigned long)rec->pc);
    console_fieldf(ctx, "lr", "Called from (LR)", "0x%08lX", (unsigned long)rec->lr);
    switch (fault_address_kind(rec->cfsr)) {
    case FAULT_ADDR_BUS:
        console_fieldf(ctx, "fault_address", "Bad address", "0x%08lX", (unsigned long)rec->bfar);
        break;
    case FAULT_ADDR_MEMMANAGE:
        console_fieldf(ctx, "fault_address", "Bad address", "0x%08lX", (unsigned long)rec->mmfar);
        break;
    case FAULT_ADDR_NONE:
    default:
        break;
    }
    console_fieldf(ctx, "crash_uptime_ms", "Crash time (ms)", "%lu", (unsigned long)rec->uptime_ms);
    console_fieldf(ctx, "cfsr", "CFSR", "0x%08lX", (unsigned long)rec->cfsr);
    console_fieldf(ctx, "hfsr", "HFSR", "0x%08lX", (unsigned long)rec->hfsr);
    return CLI_OK;
}

#ifndef NDEBUG
/* Deliberate crashes, to test the capture path. Debug builds only. */
static cli_status_t cmd_crash(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)ctx;
    static const struct {
        const char *name;
        fault_test_t kind;
    } kinds[] = {
        {"bus", FAULT_TEST_BUS},           {"usage", FAULT_TEST_USAGE},
        {"divzero", FAULT_TEST_DIVZERO},   {"hard", FAULT_TEST_HARD},
        {"watchdog", FAULT_TEST_WATCHDOG},
    };

    for (size_t i = 0U; i < (sizeof(kinds) / sizeof(kinds[0])); i++) {
        if (cli_streq(argv[1], kinds[i].name)) {
            /* Let the echo of the command reach the terminal first. */
            (void)uart_link_flush(100U);
            fault_trigger(kinds[i].kind);
            return CLI_ERR_FAILED; /* only reached if the fault didn't happen */
        }
    }
    return CLI_ERR_INVALID;
}
#endif

/* ---- can ------------------------------------------------------------------ */

#define CAN_DEFAULT_KBPS 500U
#define CAN_MAX_KBPS     1000U
#define BPS_PER_KBPS     1000U

static const char *can_bus_state(const can_drv_stats_t *s)
{
    if (can_drv_mode() == CAN_DRV_OFF) {
        return "off";
    }
    if (s->bus_off) {
        return "bus-off";
    }
    return s->error_passive ? "error-passive" : "error-active";
}

static cli_status_t can_status(void *ctx)
{
    can_drv_stats_t s;
    can_drv_get_stats(&s);
    console_fieldf(ctx, "can_mode", "Mode", "%s", can_drv_mode_name(can_drv_mode()));
    console_fieldf(ctx, "can_bitrate", "Bit rate (bit/s)", "%lu", (unsigned long)can_drv_bitrate());
    console_fieldf(ctx, "can_bus", "Bus state", "%s", can_bus_state(&s));
    console_fieldf(ctx, "can_rx_frames", "Received", "%lu", (unsigned long)s.rx_frames);
    console_fieldf(ctx, "can_rx_dropped", "  dropped", "%lu", (unsigned long)s.rx_dropped);
    console_fieldf(ctx, "can_rx_overruns", "  overruns", "%lu", (unsigned long)s.rx_overruns);
    console_fieldf(ctx, "can_queue", "  queued", "%lu/%lu", (unsigned long)can_drv_queued(),
                   (unsigned long)can_drv_queue_capacity());
    console_fieldf(ctx, "can_streamed", "Sent to laptop", "%lu", (unsigned long)can_stream_sent());
    console_fieldf(ctx, "can_tx_frames", "Transmitted", "%lu", (unsigned long)s.tx_frames);
    console_fieldf(ctx, "can_test_frames", "Test frames", "%lu/%lu", (unsigned long)can_gen_sent(),
                   (unsigned long)can_gen_count());
    console_fieldf(ctx, "can_isr_max_us", "Longest IRQ (us)", "%lu",
                   (unsigned long)timebase_cycles_to_us(s.isr_max_cycles));
    console_fieldf(ctx, "can_tec", "TX error count", "%lu", (unsigned long)s.tx_errors);
    console_fieldf(ctx, "can_rec", "RX error count", "%lu", (unsigned long)s.rx_errors);
    return CLI_OK;
}

static cli_status_t can_loopback(int argc, char *argv[])
{
    uint32_t kbps = CAN_DEFAULT_KBPS;
    if (argc > 3) {
        return CLI_ERR_ARGS;
    }
    if ((argc == 3) && !cli_parse_u32(argv[2], &kbps)) {
        return CLI_ERR_INVALID;
    }
    can_timing_t timing;
    if ((kbps == 0U) || (kbps > CAN_MAX_KBPS) ||
        !can_timing_calc(HAL_RCC_GetPCLK1Freq(), kbps * BPS_PER_KBPS, &timing)) {
        return CLI_ERR_INVALID; /* no exact bit timing for this rate */
    }
    can_gen_stop();
    return can_drv_start_loopback(kbps * BPS_PER_KBPS) ? CLI_OK : CLI_ERR_FAILED;
}

/* `can send <id> [data]`: hex, as in Linux's cansend. IDs above 7FF are
 * sent as 29-bit (extended) IDs. */
static cli_status_t can_send(int argc, char *argv[])
{
    can_frame_t frame = {.dlc = 0U};
    size_t len = 0U;
    if ((argc < 3) || (argc > 4)) {
        return CLI_ERR_ARGS;
    }
    if (!cli_parse_hex_u32(argv[2], &frame.id) || (frame.id > CAN_ID_EXT_MAX)) {
        return CLI_ERR_INVALID;
    }
    if (frame.id > CAN_ID_STD_MAX) {
        frame.flags = CAN_FLAG_EXTENDED;
    }
    if (argc == 4) {
        if (!cli_parse_hex_bytes(argv[3], frame.data, CAN_MAX_DLC, &len)) {
            return CLI_ERR_INVALID;
        }
        frame.dlc = (uint8_t)len;
    }
    /* Fails outside loopback, or if all transmit mailboxes are busy. */
    return can_drv_send(&frame) ? CLI_OK : CLI_ERR_FAILED;
}

static cli_status_t can_test(int argc, char *argv[])
{
    uint32_t count = 0U;
    uint32_t per_second = 0U;
    if ((argc < 3) || (argc > 4)) {
        return CLI_ERR_ARGS;
    }
    if (!cli_parse_u32(argv[2], &count) || ((argc == 4) && !cli_parse_u32(argv[3], &per_second))) {
        return CLI_ERR_INVALID;
    }
    if (can_drv_mode() != CAN_DRV_LOOPBACK) {
        return CLI_ERR_FAILED; /* test traffic only in loopback */
    }
    return can_gen_start(count, per_second) ? CLI_OK : CLI_ERR_INVALID;
}

static cli_status_t cmd_can(int argc, char *argv[], void *ctx)
{
    if (argc == 1) {
        return can_status(ctx);
    }
    if (cli_streq(argv[1], "loopback")) {
        return can_loopback(argc, argv);
    }
    if (cli_streq(argv[1], "off")) {
        if (argc != 2) {
            return CLI_ERR_ARGS;
        }
        can_gen_stop();
        can_drv_stop();
        return CLI_OK;
    }
    if (cli_streq(argv[1], "send")) {
        return can_send(argc, argv);
    }
    if (cli_streq(argv[1], "test")) {
        return can_test(argc, argv);
    }
    return CLI_ERR_INVALID;
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
    {"fault", "[clear]", "Details of the last crash, or forget it", 0U, 1U, cmd_fault},
    {"can", "[loopback [kbps] | off | send <id> [data] | test <count> [per_s]]",
     "CAN bus: status, loopback test mode, send a frame, test traffic", 0U, 3U, cmd_can},
    {"reset", "", "Restart the board", 0U, 0U, cmd_reset},
#ifndef NDEBUG
    {"crash", "bus|usage|divzero|hard|watchdog", "Crash on purpose to test crash capture", 1U, 1U,
     cmd_crash},
#endif
};

const cli_command_t *commands_table(void)
{
    return table;
}

size_t commands_count(void)
{
    return sizeof(table) / sizeof(table[0]);
}
