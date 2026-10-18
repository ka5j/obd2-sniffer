/*
 * OBD2 CAN sniffer: firmware entry point.
 */
#include "board.h"
#include "clock.h"
#include "console.h"
#include "reset_cause.h"
#include "version.h"

#define HEARTBEAT_HALF_PERIOD_MS 500U
#define BUTTON_DEBOUNCE_MS       30U

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

    console_init();
    print_banner(clk_src, cause);

    uint32_t last_toggle = HAL_GetTick();
    uint32_t last_btn_change = 0U;
    bool btn_state = false;

    for (;;) {
        uint32_t now = HAL_GetTick();

        /* Non-blocking 1 Hz heartbeat: the super-loop never stalls in
         * HAL_Delay(), so every task in it keeps running. */
        if ((now - last_toggle) >= HEARTBEAT_HALF_PERIOD_MS) {
            last_toggle += HEARTBEAT_HALF_PERIOD_MS;
            board_led_toggle();
        }

        /* Lockout debounce on B1: act on the first edge immediately, then
         * ignore further edges for BUTTON_DEBOUNCE_MS while contacts bounce. */
        bool raw = board_button_pressed();
        if ((raw != btn_state) && ((now - last_btn_change) >= BUTTON_DEBOUNCE_MS)) {
            btn_state = raw;
            last_btn_change = now;
            if (btn_state) {
                console_printf("[%lu ms] button pressed\r\n", (unsigned long)now);
            }
        }
    }
}
