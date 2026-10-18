/*
 * Crash record: what the CPU was doing when a fault occurred.
 *
 * When the firmware crashes (for example by reading an invalid address), the
 * fault handler copies the CPU registers and the fault status registers into
 * a record in RAM that is not cleared at reset, then resets the chip. On the
 * next boot the record is checked and reported, so a crash in the field can
 * be diagnosed after the fact.
 *
 * RAM that survives a reset contains random data after power-on, so the
 * record carries a magic number and a CRC; only a record that passes both
 * checks is trusted.
 *
 * This module is hardware-independent: it seals and validates records and
 * decodes the Cortex-M fault status registers (CFSR, HFSR) into a readable
 * cause. Capturing the registers is done by the firmware's fault handler.
 */
#ifndef FAULT_RECORD_H
#define FAULT_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#define FAULT_RECORD_MAGIC 0xFA017C0DU

typedef enum {
    FAULT_TYPE_HARD = 1,
    FAULT_TYPE_MEMMANAGE,
    FAULT_TYPE_BUS,
    FAULT_TYPE_USAGE,
} fault_type_t;

typedef struct {
    uint32_t magic;
    uint32_t type; /* fault_type_t */
    /* Registers stacked by the CPU on exception entry. */
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
    /* System Control Block fault status and address registers. */
    uint32_t cfsr, hfsr, mmfar, bfar;
    uint32_t uptime_ms; /* time since boot when the fault happened */
    uint16_t crc;       /* over everything above */
    uint16_t reserved;
} fault_record_t;

/* Set the magic number and CRC. Call after filling in the fields. */
void fault_record_seal(fault_record_t *rec);

/* True only if the magic number and CRC match. */
bool fault_record_is_valid(const fault_record_t *rec);

/* Destroy the record so it is not reported again. */
void fault_record_clear(fault_record_t *rec);

const char *fault_type_str(uint32_t type);

/* Most specific cause found in CFSR/HFSR, e.g. "divide by zero". */
const char *fault_cause_str(uint32_t cfsr, uint32_t hfsr);

typedef enum {
    FAULT_ADDR_NONE,
    FAULT_ADDR_MEMMANAGE, /* MMFAR holds the faulting address */
    FAULT_ADDR_BUS,       /* BFAR holds the faulting address */
} fault_addr_kind_t;

/* Which address register, if any, holds a valid faulting address. */
fault_addr_kind_t fault_address_kind(uint32_t cfsr);

/* ---- Cortex-M4 fault status bits (ARMv7-M ARM, B3.2.15 and B3.2.16) ---- */
/* MemManage (CFSR bits 0-7) */
#define CFSR_IACCVIOL  (1U << 0)
#define CFSR_DACCVIOL  (1U << 1)
#define CFSR_MUNSTKERR (1U << 3)
#define CFSR_MSTKERR   (1U << 4)
#define CFSR_MLSPERR   (1U << 5)
#define CFSR_MMARVALID (1U << 7)
/* BusFault (CFSR bits 8-15) */
#define CFSR_IBUSERR     (1U << 8)
#define CFSR_PRECISERR   (1U << 9)
#define CFSR_IMPRECISERR (1U << 10)
#define CFSR_UNSTKERR    (1U << 11)
#define CFSR_STKERR      (1U << 12)
#define CFSR_LSPERR      (1U << 13)
#define CFSR_BFARVALID   (1U << 15)
/* UsageFault (CFSR bits 16-31) */
#define CFSR_UNDEFINSTR (1U << 16)
#define CFSR_INVSTATE   (1U << 17)
#define CFSR_INVPC      (1U << 18)
#define CFSR_NOCP       (1U << 19)
#define CFSR_UNALIGNED  (1U << 24)
#define CFSR_DIVBYZERO  (1U << 25)
/* HardFault status */
#define HFSR_VECTTBL  (1U << 1)
#define HFSR_FORCED   (1U << 30)
#define HFSR_DEBUGEVT (1U << 31)

#endif /* FAULT_RECORD_H */
