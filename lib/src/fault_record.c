#include "fault_record.h"

#include <stddef.h>
#include <string.h>

#include "crc16.h"

static uint16_t record_crc(const fault_record_t *rec)
{
    return crc16(rec, offsetof(fault_record_t, crc));
}

void fault_record_seal(fault_record_t *rec)
{
    rec->magic = FAULT_RECORD_MAGIC;
    rec->reserved = 0U;
    rec->crc = record_crc(rec);
}

bool fault_record_is_valid(const fault_record_t *rec)
{
    return rec->magic == FAULT_RECORD_MAGIC && rec->crc == record_crc(rec);
}

void fault_record_clear(fault_record_t *rec)
{
    memset(rec, 0, sizeof *rec);
}

const char *fault_type_str(uint32_t type)
{
    switch (type) {
    case FAULT_TYPE_HARD:
        return "HardFault";
    case FAULT_TYPE_MEMMANAGE:
        return "MemManage fault";
    case FAULT_TYPE_BUS:
        return "BusFault";
    case FAULT_TYPE_USAGE:
        return "UsageFault";
    default:
        return "unknown fault";
    }
}

/* Ordered from most to least specific; the first set bit wins. */
static const struct {
    uint32_t bit;
    const char *text;
} cfsr_causes[] = {
    {CFSR_DIVBYZERO, "divide by zero"},
    {CFSR_UNALIGNED, "unaligned memory access"},
    {CFSR_UNDEFINSTR, "undefined instruction"},
    {CFSR_INVSTATE, "invalid execution state (Thumb bit clear)"},
    {CFSR_INVPC, "invalid exception return"},
    {CFSR_NOCP, "coprocessor not enabled (FPU off?)"},
    {CFSR_PRECISERR, "bus error on data access"},
    {CFSR_IMPRECISERR, "bus error on data write (imprecise)"},
    {CFSR_IBUSERR, "bus error on instruction fetch"},
    {CFSR_STKERR, "bus error while stacking (stack overflow?)"},
    {CFSR_UNSTKERR, "bus error while unstacking"},
    {CFSR_LSPERR, "bus error during FPU lazy state save"},
    {CFSR_DACCVIOL, "memory protection violation on data access"},
    {CFSR_IACCVIOL, "memory protection violation on instruction fetch"},
    {CFSR_MSTKERR, "memory protection violation while stacking"},
    {CFSR_MUNSTKERR, "memory protection violation while unstacking"},
    {CFSR_MLSPERR, "memory protection violation during FPU lazy state save"},
};

const char *fault_cause_str(uint32_t cfsr, uint32_t hfsr)
{
    for (size_t i = 0U; i < sizeof cfsr_causes / sizeof cfsr_causes[0]; i++) {
        if ((cfsr & cfsr_causes[i].bit) != 0U) {
            return cfsr_causes[i].text;
        }
    }
    if ((hfsr & HFSR_VECTTBL) != 0U) {
        return "bus error reading the vector table";
    }
    if ((hfsr & HFSR_DEBUGEVT) != 0U) {
        return "debug event (breakpoint with no debugger attached)";
    }
    return "no cause recorded";
}

fault_addr_kind_t fault_address_kind(uint32_t cfsr)
{
    if ((cfsr & CFSR_BFARVALID) != 0U) {
        return FAULT_ADDR_BUS;
    }
    if ((cfsr & CFSR_MMARVALID) != 0U) {
        return FAULT_ADDR_MEMMANAGE;
    }
    return FAULT_ADDR_NONE;
}
