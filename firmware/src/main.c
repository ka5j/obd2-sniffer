/*
 * OBD2 CAN sniffer: firmware entry point.
 *
 * Start-up brings up the clocks and the serial console, prints a report,
 * then runs a super-loop. Every task in the loop is non-blocking: it checks
 * whether it has work, does a bounded amount of it, and returns, so no task
 * can delay the others.
 */
#include "board.h"
#include "clock.h"
#include "commands.h"
#include "console.h"
#include "reset_cause.h"
#include "status_led.h"
#include "uart_link.h"
#include "version.h"

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
    if (!clock_init(&clk_src)) {
        board_panic();
    }

    if (!console_init()) {
        board_panic();
    }
    commands_set_boot_info(cause, clk_src);
    print_banner(clk_src, cause);
    console_prompt();

    for (;;) {
        uint32_t now = HAL_GetTick();

        console_poll();
        status_led_task(now);
        button_task(now);

        if (commands_reset_requested()) {
            /* Let the reply to `reset` reach the laptop first. */
            (void)uart_link_flush(RESET_FLUSH_TIMEOUT_MS);
            NVIC_SystemReset();
        }
    }
}
