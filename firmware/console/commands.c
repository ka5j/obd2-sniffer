#include "console/commands.h"

#include "can/can_drv.h"
#include "can/can_gen.h"
#include "can/can_stream.h"
#include "can/can_timing.h"
#include "console/console.h"
#include "console/console_core.h"
#include "fault/fault.h"
#include "obd/obd.h"
#include "system/status_led.h"
#include "stm32f4xx_hal.h"
#include "system/timebase.h"
#include "console/uart_link.h"
#include "version.h"
#include "system/watchdog.h"

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
    console_fieldf(ctx, "firmware", "%s", FW_VERSION);
    console_fieldf(ctx, "build", "%s", BUILD_TYPE);
    console_fieldf(ctx, "mcu", "STM32F446RE");
    console_fieldf(ctx, "revision", "0x%04lX", (unsigned long)HAL_GetREVID());
    console_fieldf(ctx, "uid", "%08lX-%08lX-%08lX", (unsigned long)HAL_GetUIDw2(),
                   (unsigned long)HAL_GetUIDw1(), (unsigned long)HAL_GetUIDw0());
    console_fieldf(ctx, "compiler", "GCC %s", __VERSION__);
    return CLI_OK;
}

static cli_status_t cmd_status(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)argv;
    uart_link_stats_t link;
    uart_link_get_stats(&link);

    console_fieldf(ctx, "uptime_ms", "%lu", (unsigned long)HAL_GetTick());
    console_fieldf(ctx, "reset_cause", "%s", reset_cause_name(boot_reset_cause));
    console_fieldf(ctx, "clock", "%s", clock_source_name(boot_clock));
    console_fieldf(ctx, "sysclk_hz", "%lu", (unsigned long)HAL_RCC_GetSysClockFreq());
    console_fieldf(ctx, "watchdog_timeout_ms", "%lu", (unsigned long)WATCHDOG_TIMEOUT_MS);
    console_fieldf(ctx, "last_fault", "%s",
                   (fault_last() != NULL) ? fault_type_str(fault_last()->type) : "none");
    console_fieldf(ctx, "uart_rx_bytes", "%lu", (unsigned long)link.rx_bytes);
    console_fieldf(ctx, "uart_rx_dropped", "%lu", (unsigned long)link.rx_dropped);
    console_fieldf(ctx, "uart_rx_overruns", "%lu", (unsigned long)link.rx_overruns);
    console_fieldf(ctx, "uart_rx_errors", "%lu", (unsigned long)link.rx_errors);
    console_fieldf(ctx, "uart_tx_bytes", "%lu", (unsigned long)link.tx_bytes);
    console_fieldf(ctx, "uart_tx_dropped", "%lu", (unsigned long)link.tx_dropped);
    console_fieldf(ctx, "uart_tx_dma_errors", "%lu", (unsigned long)link.tx_dma_errors);
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
        console_fieldf(ctx, "fault", "none");
        return CLI_OK;
    }
    console_fieldf(ctx, "fault", "%s", fault_type_str(rec->type));
    console_fieldf(ctx, "cause", "%s", fault_cause_str(rec->cfsr, rec->hfsr));
    console_fieldf(ctx, "pc", "0x%08lX", (unsigned long)rec->pc);
    console_fieldf(ctx, "lr", "0x%08lX", (unsigned long)rec->lr);
    switch (fault_address_kind(rec->cfsr)) {
    case FAULT_ADDR_BUS:
        console_fieldf(ctx, "fault_address", "0x%08lX", (unsigned long)rec->bfar);
        break;
    case FAULT_ADDR_MEMMANAGE:
        console_fieldf(ctx, "fault_address", "0x%08lX", (unsigned long)rec->mmfar);
        break;
    case FAULT_ADDR_NONE:
    default:
        break;
    }
    console_fieldf(ctx, "crash_uptime_ms", "%lu", (unsigned long)rec->uptime_ms);
    console_fieldf(ctx, "cfsr", "0x%08lX", (unsigned long)rec->cfsr);
    console_fieldf(ctx, "hfsr", "0x%08lX", (unsigned long)rec->hfsr);
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
    console_fieldf(ctx, "can_mode", "%s", can_drv_mode_name(can_drv_mode()));
    console_fieldf(ctx, "can_bitrate", "%lu", (unsigned long)can_drv_bitrate());
    console_fieldf(ctx, "can_bus", "%s", can_bus_state(&s));
    console_fieldf(ctx, "can_rx_frames", "%lu", (unsigned long)s.rx_frames);
    console_fieldf(ctx, "can_rx_dropped", "%lu", (unsigned long)s.rx_dropped);
    console_fieldf(ctx, "can_rx_overruns", "%lu", (unsigned long)s.rx_overruns);
    console_fieldf(ctx, "can_queue", "%lu/%lu", (unsigned long)can_drv_queued(),
                   (unsigned long)can_drv_queue_capacity());
    console_fieldf(ctx, "can_stream", "%s", can_stream_enabled() ? "on" : "off");
    console_fieldf(ctx, "can_streamed", "%lu", (unsigned long)can_stream_sent());
    console_fieldf(ctx, "can_tx_frames", "%lu", (unsigned long)s.tx_frames);
    console_fieldf(ctx, "can_test_frames", "%lu/%lu", (unsigned long)can_gen_sent(),
                   (unsigned long)can_gen_count());
    console_fieldf(ctx, "can_isr_max_us", "%lu",
                   (unsigned long)timebase_cycles_to_us(s.isr_max_cycles));
    console_fieldf(ctx, "can_tec", "%lu", (unsigned long)s.tx_errors);
    console_fieldf(ctx, "can_rec", "%lu", (unsigned long)s.rx_errors);
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

/* `can stream on|off`: whether received frames are sent to the terminal or
 * the laptop. */
static cli_status_t can_stream(int argc, char *argv[])
{
    if (argc != 3) {
        return CLI_ERR_ARGS;
    }
    if (cli_streq(argv[2], "on")) {
        can_stream_enable(true);
        return CLI_OK;
    }
    if (cli_streq(argv[2], "off")) {
        can_stream_enable(false);
        return CLI_OK;
    }
    return CLI_ERR_INVALID;
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
        obd_stop();
        can_drv_stop();
        return CLI_OK;
    }
    if (cli_streq(argv[1], "test")) {
        return can_test(argc, argv);
    }
    if (cli_streq(argv[1], "stream")) {
        return can_stream(argc, argv);
    }
    return CLI_ERR_INVALID;
}

/* ---- obd ------------------------------------------------------------------ */

#define DTC_LIST_LEN (OBD_CLIENT_DTC_MAX * OBD2_DTC_TEXT_LEN) /* codes, spaces, terminator */

/* One live value per PID in lib/obd/obd2.c's table, under its key there:
 * "-" until the tester has read it. */
static void obd_values(void *ctx, const obd_client_t *t)
{
    for (size_t i = 0U; i < OBD2_PID_COUNT; i++) {
        const obd2_pid_t *pid = obd2_pid_at(i);
        int32_t value = t->values[i];
        if (!t->value_valid[i]) {
            console_fieldf(ctx, pid->key, "-");
        } else if (pid->decimals == 1U) {
            console_fieldf(ctx, pid->key, "%ld.%ld", (long)(value / 10),
                           (long)(value % 10)); /* percentages: never negative */
        } else {
            console_fieldf(ctx, pid->key, "%ld", (long)value);
        }
    }
}

/* The stored trouble codes as text, e.g. "P0301 P0420". */
static void dtc_list(const obd_client_t *t, char out[DTC_LIST_LEN])
{
    size_t pos = 0U;
    out[0] = '\0';
    for (uint8_t i = 0U; i < t->dtc_count; i++) {
        if (i > 0U) {
            out[pos] = ' ';
            pos++;
        }
        obd2_dtc_text(t->dtcs[i], &out[pos]); /* terminated after each code */
        pos += OBD2_DTC_TEXT_LEN - 1U;
    }
}

static cli_status_t obd_status(void *ctx)
{
    const obd_client_t *t = obd_tester();
    char codes[DTC_LIST_LEN];
    dtc_list(t, codes);
    const char *codes_text = "-"; /* not read yet */
    if (t->dtcs_valid) {
        codes_text = (t->dtc_count == 0U) ? "none" : codes;
    }

    console_fieldf(ctx, "obd_tester", "%s", obd_client_running(t) ? "running" : "stopped");
    console_fieldf(ctx, "obd_ecu", "%s", obd_ecu_on() ? "on" : "off");
    obd_values(ctx, t);
    if (t->status_valid) {
        console_fieldf(ctx, "obd_mil", "%s", t->mil_on ? "on" : "off");
        console_fieldf(ctx, "obd_dtc_count", "%u", (unsigned int)t->dtc_reported);
    } else {
        console_fieldf(ctx, "obd_mil", "-");
        console_fieldf(ctx, "obd_dtc_count", "-");
    }
    console_fieldf(ctx, "obd_dtcs", "%s", codes_text);
    console_fieldf(ctx, "obd_vin", "%s", t->vin_valid ? t->vin : "-");
    console_fieldf(ctx, "obd_requests", "%lu", (unsigned long)t->requests);
    console_fieldf(ctx, "obd_replies", "%lu", (unsigned long)t->replies);
    console_fieldf(ctx, "obd_timeouts", "%lu", (unsigned long)t->timeouts);
    console_fieldf(ctx, "obd_errors", "%lu", (unsigned long)t->errors);
    return CLI_OK;
}

static cli_status_t cmd_obd(int argc, char *argv[], void *ctx)
{
    if (argc == 1) {
        return obd_status(ctx);
    }
    if (cli_streq(argv[1], "start")) {
        return obd_start() ? CLI_OK : CLI_ERR_FAILED; /* CAN must be in loopback */
    }
    if (cli_streq(argv[1], "stop")) {
        obd_stop();
        return CLI_OK;
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
    {"fault", "[clear]", "Details of the last crash, or forget it", 0U, 1U, cmd_fault},
    {"can", "[loopback [kbps] | off | test <count> [per_s] | stream on|off]",
     "CAN bus: status, loopback, test traffic, frame output", 0U, 3U, cmd_can},
    {"obd", "[start|stop]", "OBD2 tester: live data, trouble codes, VIN (loopback)", 0U, 1U,
     cmd_obd},
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
