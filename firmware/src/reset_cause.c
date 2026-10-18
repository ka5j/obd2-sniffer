#include "reset_cause.h"

#include "stm32f4xx_hal.h"

reset_cause_t reset_cause_read(void)
{
    reset_cause_t cause;

    /* Several flags can be set at once: a power-on also sets BOR and PIN, and
     * every internal reset also drives NRST low and sets PIN. Check the most
     * specific causes first. */
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) {
        cause = RESET_CAUSE_LOW_POWER;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) {
        cause = RESET_CAUSE_WWDG;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
        cause = RESET_CAUSE_IWDG;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) {
        cause = RESET_CAUSE_SOFTWARE;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST)) {
        cause = RESET_CAUSE_POWER_ON;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST)) {
        cause = RESET_CAUSE_BROWN_OUT;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)) {
        cause = RESET_CAUSE_PIN;
    } else {
        cause = RESET_CAUSE_UNKNOWN;
    }

    /* Flags are sticky until cleared; clear them so the next boot reports
     * only its own cause. */
    __HAL_RCC_CLEAR_RESET_FLAGS();
    return cause;
}

const char *reset_cause_name(reset_cause_t cause)
{
    switch (cause) {
    case RESET_CAUSE_POWER_ON:
        return "power-on";
    case RESET_CAUSE_BROWN_OUT:
        return "brown-out";
    case RESET_CAUSE_PIN:
        return "reset pin";
    case RESET_CAUSE_SOFTWARE:
        return "software";
    case RESET_CAUSE_IWDG:
        return "independent watchdog";
    case RESET_CAUSE_WWDG:
        return "window watchdog";
    case RESET_CAUSE_LOW_POWER:
        return "low-power";
    case RESET_CAUSE_UNKNOWN:
    default:
        return "unknown";
    }
}
