/*
 * Why did the MCU last reset? Read once at boot from RCC_CSR.
 *
 * Knowing the reset cause is the first step of field diagnostics: a watchdog
 * or software reset that nobody asked for means something went wrong.
 */
#ifndef RESET_CAUSE_H
#define RESET_CAUSE_H

typedef enum {
    RESET_CAUSE_UNKNOWN,
    RESET_CAUSE_POWER_ON,
    RESET_CAUSE_BROWN_OUT,
    RESET_CAUSE_PIN,       /* NRST pin: reset button or debugger */
    RESET_CAUSE_SOFTWARE,  /* NVIC_SystemReset() */
    RESET_CAUSE_IWDG,      /* independent watchdog timeout */
    RESET_CAUSE_WWDG,      /* window watchdog */
    RESET_CAUSE_LOW_POWER, /* illegal low-power mode entry */
} reset_cause_t;

/* Read and clear the reset flags. Call once, early in main(). */
reset_cause_t reset_cause_read(void);

const char *reset_cause_name(reset_cause_t cause);

#endif /* RESET_CAUSE_H */
