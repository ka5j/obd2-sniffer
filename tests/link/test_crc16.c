#include <stdint.h>

#include "link/crc16.h"
#include "test_support.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* Reference values from the CRC catalogue / an independent Python model. */

static void test_standard_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1U, crc16((const uint8_t *)"123456789", 9U));
}

static void test_known_vectors(void)
{
    const uint8_t zeros[4] = {0};
    uint8_t ramp[256];
    for (int i = 0; i < 256; i++) {
        ramp[i] = (uint8_t)i;
    }
    TEST_ASSERT_EQUAL_HEX16(0xB915U, crc16((const uint8_t *)"A", 1U));
    TEST_ASSERT_EQUAL_HEX16(0x84C0U, crc16(zeros, sizeof zeros));
    TEST_ASSERT_EQUAL_HEX16(0x3FBDU, crc16(ramp, sizeof ramp));
}

static void test_empty_input_returns_initial_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(CRC16_INIT, crc16((const uint8_t *)"", 0U));
}

static void test_incremental_matches_one_shot(void)
{
    uint16_t crc = crc16_update(CRC16_INIT, (const uint8_t *)"1234", 4U);
    crc = crc16_update(crc, (const uint8_t *)"56789", 5U);
    TEST_ASSERT_EQUAL_HEX16(0x29B1U, crc);
}

static void test_single_bit_change_is_detected(void)
{
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint16_t before = crc16(data, sizeof data);
    data[5] ^= 0x10U;
    TEST_ASSERT_NOT_EQUAL(before, crc16(data, sizeof data));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_standard_check_value);
    UNIT_TEST(test_known_vectors);
    UNIT_TEST(test_empty_input_returns_initial_value);
    UNIT_TEST(test_incremental_matches_one_shot);
    UNIT_TEST(test_single_bit_change_is_detected);
    return UNITY_END();
}
