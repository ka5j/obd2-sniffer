/*
 * OBD2 CAN sniffer: firmware entry point.
 *
 * Start-up brings up the clocks and the serial console, prints a report,
 * then runs a super-loop. Every task in the loop is non-blocking: it checks
 * whether it has work, does a bounded amount of it, and returns, so no task
 * can delay the others.
 */
#include "board.h"
#include "can_drv.h"
#include "can_gen.h"
#include "can_stream.h"
#include "clock.h"
#include "commands.h"
#include "console.h"
#include "fault.h"
#include "reset_cause.h"
#include "status_led.h"
#include "timebase.h"
#include "uart_link.h"
#include "version.h"
#include "watchdog.h"

#define BUTTON_DEBOUNCE_MS     30U
#define RESET_FLUSH_TIMEOUT_MS 100U

static void print_banner(clock_source_t clk_src, reset_cause_t cause)
{
    console_puts("\r\n========================================\r\n");
    console_printf(" OBD2 CAN Sniffer  %s\r\n", FW_VERSION);
    console_puts("----------------------------------------\r\n");
    console_printf(" MCU      STM32F446RE  rev 0x%04lX\r\n", (unsigned long)HAL_GetREVID());
    console_printf(" UID      %08lX-%08lX-%08lX\r\n", (unsigned long)HAL_GetUIDw2(),
                   (unsigned long)HAL_GetUIDw1(), (unsigned long)HAL_GetUIDw0());
    console_printf(" Clock    %s\r\n", clock_source_name(clk_src));
    console_printf(" SYSCLK   %lu MHz\r\n", (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000U));
    console_printf(" PCLK1    %lu MHz   PCLK2 %lu MHz\r\n",
                   (unsigned long)(HAL_RCC_GetPCLK1Freq() / 1000000U),
                   (unsigned long)(HAL_RCC_GetPCLK2Freq() / 1000000U));
    console_printf(" Reset    %s\r\n", reset_cause_name(cause));

    const fault_record_t *crash = fault_last();
    if (crash != NULL) {
        console_printf(" Crash    %s at 0x%08lX: %s\r\n", fault_type_str(crash->type),
                       (unsigned long)crash->pc, fault_cause_str(crash->cfsr, crash->hfsr));
        console_puts("          details: 'fault'\r\n");
    }
    console_puts("========================================\r\n");
    console_puts("Type 'help' for a list of commands.\r\n");
}

/* Report presses of the blue button. Lockout debounce: act on the first
 * edge immediately, then ignore further edges while the contacts bounce. */
static void button_task(uint32_t now_ms)
{
    static bool pressed;
    static uint32_t last_change_ms;

    bool raw = board_button_pressed();
    if ((raw != pressed) && ((now_ms - last_change_ms) >= BUTTON_DEBOUNCE_MS)) {
        pressed = raw;
        last_change_ms = now_ms;
        if (pressed) {
            console_event("button pressed");
        }
    }
}

int main(void)
{
    /* Read the reset flags before anything else can disturb them. */
    reset_cause_t cause = reset_cause_read();

    HAL_StatusTypeDef hal = HAL_Init();
    board_init();
    if (hal != HAL_OK) {
        board_panic();
    }

    clock_source_t clk_src;
    if (!clock_init(&clk_src) || !timebase_init()) {
        board_panic();
    }
    fault_init();

    /* CAN stays off until a command starts it. */
    if (!console_init() || !can_drv_init()) {
        board_panic();
    }
    commands_set_boot_info(cause, clk_src);
    print_banner(clk_src, cause);
    console_prompt();

    /* Started last, once start-up can no longer take long. */
    if (!watchdog_init()) {
        board_panic();
    }

    for (;;) {
        uint32_t now = HAL_GetTick();

        watchdog_kick();
        console_poll();
        can_gen_task();
        can_stream_task(now);
        status_led_task(now);
        button_task(now);

        if (commands_reset_requested()) {
            /* Let the reply to `reset` reach the laptop first. */
            (void)uart_link_flush(RESET_FLUSH_TIMEOUT_MS);
            NVIC_SystemReset();
        }
    }
}
