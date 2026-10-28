#include "fault/fault.h"

#include <stdbool.h>

#include "stm32f4xx_hal.h"

/* Reading here raises a precise BusFault: it is just past the end of the
 * 512 KiB flash, in a reserved part of the memory map (RM0390 2.3). */
#define FAULT_TEST_BAD_ADDRESS 0x08080000U
/* An odd address in SRAM: a 32-bit read from it is unaligned. */
#define FAULT_TEST_UNALIGNED_ADDRESS 0x20000001U

/* Kept across warm resets: the startup code never initialises .noinit. */
static fault_record_t noinit_record __attribute__((section(".noinit")));

static fault_record_t last_record;
static bool have_last;

/* Called from fault_entry.S, on the dedicated fault stack, with the exception stack frame (r0, r1,
 * r2, r3, r12, lr, pc, xpsr as stacked by the CPU) and the fault type. Saves everything and
 * restarts the chip. */
__attribute__((noreturn)) void fault_capture(const uint32_t *frame, uint32_t type);

void fault_capture(const uint32_t *frame, uint32_t type)
{
    fault_record_t *rec = &noinit_record;

    rec->type = type;
    rec->r0 = frame[0];
    rec->r1 = frame[1];
    rec->r2 = frame[2];
    rec->r3 = frame[3];
    rec->r12 = frame[4];
    rec->lr = frame[5];
    rec->pc = frame[6];
    rec->xpsr = frame[7];
    rec->cfsr = SCB->CFSR;
    rec->hfsr = SCB->HFSR;
    rec->mmfar = SCB->MMFAR;
    rec->bfar = SCB->BFAR;
    rec->uptime_ms = HAL_GetTick();
    fault_record_seal(rec);

    NVIC_SystemReset();
}

/* The entry stubs in fault_entry.S pass these numbers in r1. */
_Static_assert(FAULT_TYPE_HARD == 1U, "fault_entry.S: HardFault_Handler type");
_Static_assert(FAULT_TYPE_MEMMANAGE == 2U, "fault_entry.S: MemManage_Handler type");
_Static_assert(FAULT_TYPE_BUS == 3U, "fault_entry.S: BusFault_Handler type");
_Static_assert(FAULT_TYPE_USAGE == 4U, "fault_entry.S: UsageFault_Handler type");

/* ---- start-up and reporting ---------------------------------------------- */

void fault_init(void)
{
    if (fault_record_is_valid(&noinit_record)) {
        last_record = noinit_record;
        have_last = true;
    }
    /* Report a crash once: clear it so a later reset doesn't repeat it. */
    fault_record_clear(&noinit_record);

    /* Without these, every fault escalates to HardFault and the specific
     * cause is harder to tell apart. */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    /* Integer division by zero returns 0 unless this trap is enabled. */
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
}

const fault_record_t *fault_last(void)
{
    return have_last ? &last_record : NULL;
}

void fault_clear(void)
{
    have_last = false;
    fault_record_clear(&last_record);
}

/* ---- deliberate faults (testing) ------------------------------------------ */

static void read_bad_address(void)
{
    volatile const uint32_t *bad = (volatile const uint32_t *)FAULT_TEST_BAD_ADDRESS;
    (void)*bad;
}

void fault_trigger(fault_test_t kind)
{
    switch (kind) {
    case FAULT_TEST_BUS:
        read_bad_address();
        break;
    case FAULT_TEST_USAGE: {
        /* Unaligned word accesses are normally allowed; with the trap enabled
         * they raise a UsageFault. */
        SCB->CCR |= SCB_CCR_UNALIGN_TRP_Msk;
        volatile const uint32_t *odd = (volatile const uint32_t *)FAULT_TEST_UNALIGNED_ADDRESS;
        (void)*odd;
        break;
    }
    case FAULT_TEST_DIVZERO: {
        /* Deliberate division by zero (undefined behaviour in C), trapped by
         * the CPU: deviation DEV-04 in misra/config.toml. */
        volatile uint32_t divisor = 0U;
        // cppcheck-suppress zerodiv ; intentional, see DEV-04
        volatile uint32_t result = 100U / divisor;
        (void)result;
        break;
    }
    case FAULT_TEST_HARD:
        /* With the BusFault handler disabled, the bus error escalates. */
        SCB->SHCSR &= ~SCB_SHCSR_BUSFAULTENA_Msk;
        read_bad_address();
        break;
    case FAULT_TEST_WATCHDOG:
    default:
        __disable_irq();
        for (;;) {
            /* The main loop no longer runs, so the watchdog is never
             * refreshed and resets the chip. */
        }
    }
}
