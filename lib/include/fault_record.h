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

/* Values of fault_record_t.type */
#define FAULT_TYPE_HARD      1U
#define FAULT_TYPE_MEMMANAGE 2U
#define FAULT_TYPE_BUS       3U
#define FAULT_TYPE_USAGE     4U

typedef struct {
    uint32_t magic;
    uint32_t type; /* FAULT_TYPE_* */
    /* Registers stacked by the CPU on exception entry. */
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
    /* System Control Block fault status and address registers. */
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
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

/* ---- Cortex-M4 fault status bits (ARMv7-M ARM, B3.2.15 and B3.2.16) ----
 * Written as full 32-bit constants so every value has the same width. */
/* MemManage (CFSR bits 0-7) */
#define CFSR_IACCVIOL  0x00000001U
#define CFSR_DACCVIOL  0x00000002U
#define CFSR_MUNSTKERR 0x00000008U
#define CFSR_MSTKERR   0x00000010U
#define CFSR_MLSPERR   0x00000020U
#define CFSR_MMARVALID 0x00000080U
/* BusFault (CFSR bits 8-15) */
#define CFSR_IBUSERR     0x00000100U
#define CFSR_PRECISERR   0x00000200U
#define CFSR_IMPRECISERR 0x00000400U
#define CFSR_UNSTKERR    0x00000800U
#define CFSR_STKERR      0x00001000U
#define CFSR_LSPERR      0x00002000U
#define CFSR_BFARVALID   0x00008000U
/* UsageFault (CFSR bits 16-31) */
#define CFSR_UNDEFINSTR 0x00010000U
#define CFSR_INVSTATE   0x00020000U
#define CFSR_INVPC      0x00040000U
#define CFSR_NOCP       0x00080000U
#define CFSR_UNALIGNED  0x01000000U
#define CFSR_DIVBYZERO  0x02000000U
/* HardFault status */
#define HFSR_VECTTBL  0x00000002U
#define HFSR_FORCED   0x40000000U
#define HFSR_DEBUGEVT 0x80000000U

#endif /* FAULT_RECORD_H */
