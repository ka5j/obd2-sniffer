#include <stddef.h>
#include <string.h>

#include "crc16.h"
#include "fault_record.h"
#include "test_support.h"

static fault_record_t rec;

void setUp(void)
{
    memset(&rec, 0, sizeof rec);
    rec.type = FAULT_TYPE_BUS;
    rec.pc = 0x08001234U;
    rec.lr = 0x08000F01U;
    rec.cfsr = CFSR_PRECISERR | CFSR_BFARVALID;
    rec.bfar = 0xDEAD0000U;
    rec.uptime_ms = 4242U;
}

void tearDown(void)
{
}

/* ---- sealing and validation ------------------------------------------------ */

static void test_sealed_record_is_valid(void)
{
    fault_record_seal(&rec);
    TEST_ASSERT_EQUAL_HEX32(FAULT_RECORD_MAGIC, rec.magic);
    TEST_ASSERT_TRUE(fault_record_is_valid(&rec));
}

static void test_unsealed_or_random_memory_is_invalid(void)
{
    TEST_ASSERT_FALSE(fault_record_is_valid(&rec)); /* no magic */
    memset(&rec, 0xA5, sizeof rec);                 /* typical power-on garbage */
    TEST_ASSERT_FALSE(fault_record_is_valid(&rec));
}

static void test_corrupted_field_is_detected(void)
{
    fault_record_seal(&rec);
    rec.pc ^= 0x4U;
    TEST_ASSERT_FALSE(fault_record_is_valid(&rec));
}

static void test_wrong_magic_with_matching_crc_is_invalid(void)
{
    rec.magic = 0x12345678U;
    rec.crc = crc16((const uint8_t *)&rec, offsetof(fault_record_t, crc)); /* CRC itself is fine */
    TEST_ASSERT_FALSE(fault_record_is_valid(&rec));
}

static void test_clear_invalidates(void)
{
    fault_record_seal(&rec);
    fault_record_clear(&rec);
    TEST_ASSERT_FALSE(fault_record_is_valid(&rec));
    TEST_ASSERT_EQUAL_UINT32(0U, rec.pc);
}

/* ---- names ------------------------------------------------------------------ */

static void test_type_names(void)
{
    TEST_ASSERT_EQUAL_STRING("HardFault", fault_type_str(FAULT_TYPE_HARD));
    TEST_ASSERT_EQUAL_STRING("MemManage fault", fault_type_str(FAULT_TYPE_MEMMANAGE));
    TEST_ASSERT_EQUAL_STRING("BusFault", fault_type_str(FAULT_TYPE_BUS));
    TEST_ASSERT_EQUAL_STRING("UsageFault", fault_type_str(FAULT_TYPE_USAGE));
    TEST_ASSERT_EQUAL_STRING("unknown fault", fault_type_str(0U));
}

/* ---- cause decoding ---------------------------------------------------------- */

static void test_each_cfsr_bit_has_a_cause(void)
{
    static const struct {
        uint32_t bit;
        const char *text;
    } cases[] = {
        {CFSR_IACCVIOL, "memory protection violation on instruction fetch"},
        {CFSR_DACCVIOL, "memory protection violation on data access"},
        {CFSR_MUNSTKERR, "memory protection violation while unstacking"},
        {CFSR_MSTKERR, "memory protection violation while stacking"},
        {CFSR_MLSPERR, "memory protection violation during FPU lazy state save"},
        {CFSR_IBUSERR, "bus error on instruction fetch"},
        {CFSR_PRECISERR, "bus error on data access"},
        {CFSR_IMPRECISERR, "bus error on data write (imprecise)"},
        {CFSR_UNSTKERR, "bus error while unstacking"},
        {CFSR_STKERR, "bus error while stacking (stack overflow?)"},
        {CFSR_LSPERR, "bus error during FPU lazy state save"},
        {CFSR_UNDEFINSTR, "undefined instruction"},
        {CFSR_INVSTATE, "invalid execution state (Thumb bit clear)"},
        {CFSR_INVPC, "invalid exception return"},
        {CFSR_NOCP, "coprocessor not enabled (FPU off?)"},
        {CFSR_UNALIGNED, "unaligned memory access"},
        {CFSR_DIVBYZERO, "divide by zero"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        TEST_ASSERT_EQUAL_STRING(cases[i].text, fault_cause_str(cases[i].bit, 0U));
    }
}

static void test_most_specific_cause_wins(void)
{
    TEST_ASSERT_EQUAL_STRING("divide by zero",
                             fault_cause_str(CFSR_DIVBYZERO | CFSR_PRECISERR, HFSR_FORCED));
}

static void test_valid_flags_alone_are_not_a_cause(void)
{
    TEST_ASSERT_EQUAL_STRING("no cause recorded",
                             fault_cause_str(CFSR_BFARVALID | CFSR_MMARVALID, 0U));
}

static void test_hardfault_only_causes(void)
{
    TEST_ASSERT_EQUAL_STRING("bus error reading the vector table",
                             fault_cause_str(0U, HFSR_VECTTBL));
    TEST_ASSERT_EQUAL_STRING("debug event (breakpoint with no debugger attached)",
                             fault_cause_str(0U, HFSR_DEBUGEVT));
    TEST_ASSERT_EQUAL_STRING("fault escalated to HardFault, no further detail",
                             fault_cause_str(0U, HFSR_FORCED));
    TEST_ASSERT_EQUAL_STRING("no cause recorded", fault_cause_str(0U, 0U));
}

/* ---- faulting address ------------------------------------------------------- */

static void test_address_kind(void)
{
    TEST_ASSERT_EQUAL(FAULT_ADDR_BUS, fault_address_kind(CFSR_PRECISERR | CFSR_BFARVALID));
    TEST_ASSERT_EQUAL(FAULT_ADDR_MEMMANAGE, fault_address_kind(CFSR_DACCVIOL | CFSR_MMARVALID));
    TEST_ASSERT_EQUAL(FAULT_ADDR_BUS, fault_address_kind(CFSR_BFARVALID | CFSR_MMARVALID));
    TEST_ASSERT_EQUAL(FAULT_ADDR_NONE, fault_address_kind(CFSR_IMPRECISERR));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_sealed_record_is_valid);
    UNIT_TEST(test_unsealed_or_random_memory_is_invalid);
    UNIT_TEST(test_corrupted_field_is_detected);
    UNIT_TEST(test_wrong_magic_with_matching_crc_is_invalid);
    UNIT_TEST(test_clear_invalidates);
    UNIT_TEST(test_type_names);
    UNIT_TEST(test_each_cfsr_bit_has_a_cause);
    UNIT_TEST(test_most_specific_cause_wins);
    UNIT_TEST(test_valid_flags_alone_are_not_a_cause);
    UNIT_TEST(test_hardfault_only_causes);
    UNIT_TEST(test_address_kind);
    return UNITY_END();
}
