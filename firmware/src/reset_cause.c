#include "reset_cause.h"

#include "stm32f4xx.h"

reset_cause_t reset_cause_read(void)
{
    const uint32_t csr = RCC->CSR;
    reset_cause_t cause;

    /* Several flags can be set at once: a power-on also sets BOR and PIN, and
     * every internal reset also drives NRST low and sets PIN. Check the most
     * specific causes first. */
    if ((csr & RCC_CSR_LPWRRSTF) != 0U) {
        cause = RESET_CAUSE_LOW_POWER;
    } else if ((csr & RCC_CSR_WWDGRSTF) != 0U) {
        cause = RESET_CAUSE_WWDG;
    } else if ((csr & RCC_CSR_IWDGRSTF) != 0U) {
        cause = RESET_CAUSE_IWDG;
    } else if ((csr & RCC_CSR_SFTRSTF) != 0U) {
        cause = RESET_CAUSE_SOFTWARE;
    } else if ((csr & RCC_CSR_PORRSTF) != 0U) {
        cause = RESET_CAUSE_POWER_ON;
    } else if ((csr & RCC_CSR_BORRSTF) != 0U) {
        cause = RESET_CAUSE_BROWN_OUT;
    } else if ((csr & RCC_CSR_PINRSTF) != 0U) {
        cause = RESET_CAUSE_PIN;
    } else {
        cause = RESET_CAUSE_UNKNOWN;
    }

    /* Flags are sticky until cleared; clear them so the next boot reports
     * only its own cause. */
    RCC->CSR |= RCC_CSR_RMVF;
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
