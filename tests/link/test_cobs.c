#include <stdint.h>
#include <string.h>

#include "link/cobs.h"
#include "test_support.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static uint8_t encoded[300];
static uint8_t decoded[300];
static size_t n;

/* Encode, check against the expected bytes, then decode back. */
static void check_round_trip(const uint8_t *in, size_t len, const uint8_t *expected,
                             size_t expected_len)
{
    TEST_ASSERT_TRUE(cobs_encode(in, len, encoded, sizeof encoded, &n));
    TEST_ASSERT_EQUAL_size_t(expected_len, n);
    TEST_ASSERT_EQUAL_MEMORY(expected, encoded, expected_len);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(COBS_MAX_ENCODED(len), n);
    for (size_t i = 0U; i < n; i++) {
        TEST_ASSERT_NOT_EQUAL(0, encoded[i]);
    }
    TEST_ASSERT_TRUE(cobs_decode(encoded, n, decoded, sizeof decoded, &n));
    TEST_ASSERT_EQUAL_size_t(len, n);
    if (len > 0U) {
        TEST_ASSERT_EQUAL_MEMORY(in, decoded, len);
    }
}

/* ---- the reference examples (Wikipedia, "Consistent Overhead Byte Stuffing") */

static void test_reference_examples(void)
{
    check_round_trip((const uint8_t *)"", 0U, (const uint8_t *)"\x01", 1U);
    check_round_trip((const uint8_t *)"\x00", 1U, (const uint8_t *)"\x01\x01", 2U);
    check_round_trip((const uint8_t *)"\x00\x00", 2U, (const uint8_t *)"\x01\x01\x01", 3U);
    check_round_trip((const uint8_t *)"\x00\x11\x00", 3U, (const uint8_t *)"\x01\x02\x11\x01", 4U);
    check_round_trip((const uint8_t *)"\x11\x22\x00\x33", 4U,
                     (const uint8_t *)"\x03\x11\x22\x02\x33", 5U);
    check_round_trip((const uint8_t *)"\x11\x22\x33\x44", 4U,
                     (const uint8_t *)"\x05\x11\x22\x33\x44", 5U);
    check_round_trip((const uint8_t *)"\x11\x00\x00\x00", 4U,
                     (const uint8_t *)"\x02\x11\x01\x01\x01", 5U);
}

/* Runs of 254 non-zero bytes fill a block exactly. */
static void test_full_blocks(void)
{
    uint8_t in[255];
    uint8_t expected[258];

    /* 01..FE: one full block, nothing after it */
    for (uint32_t i = 0U; i < 254U; i++) {
        in[i] = (uint8_t)(i + 1U);
    }
    expected[0] = 0xFFU;
    memcpy(&expected[1], in, 254U);
    check_round_trip(in, 254U, expected, 255U);

    /* 00 01..FE: a zero, then one full block */
    in[0] = 0U;
    for (uint32_t i = 1U; i < 255U; i++) {
        in[i] = (uint8_t)i;
    }
    expected[0] = 0x01U;
    expected[1] = 0xFFU;
    memcpy(&expected[2], &in[1], 254U);
    check_round_trip(in, 255U, expected, 256U);

    /* 01..FF: a full block, then a block with the last byte */
    for (uint32_t i = 0U; i < 255U; i++) {
        in[i] = (uint8_t)(i + 1U);
    }
    expected[0] = 0xFFU;
    memcpy(&expected[1], in, 254U);
    expected[255] = 0x02U;
    expected[256] = 0xFFU;
    check_round_trip(in, 255U, expected, 257U);
}

/* ---- errors ------------------------------------------------------------------- */

static void test_encode_rejects_small_output(void)
{
    const uint8_t in[] = {0x11, 0x00, 0x22};
    TEST_ASSERT_FALSE(cobs_encode(in, 3U, encoded, 0U, &n));
    TEST_ASSERT_FALSE(cobs_encode(in, 3U, encoded, 1U, &n)); /* no room for 0x11 */
    TEST_ASSERT_FALSE(cobs_encode(in, 3U, encoded, 2U, &n)); /* no room for the next code */
    TEST_ASSERT_TRUE(cobs_encode(in, 3U, encoded, 4U, &n));
    TEST_ASSERT_EQUAL_size_t(4U, n);
}

static void test_decode_rejects_invalid_input(void)
{
    TEST_ASSERT_FALSE(cobs_decode((const uint8_t *)"", 0U, decoded, sizeof decoded, &n));
    TEST_ASSERT_FALSE(cobs_decode((const uint8_t *)"\x00", 1U, decoded, sizeof decoded, &n));
    /* block claims 3 data bytes, only 1 present */
    TEST_ASSERT_FALSE(cobs_decode((const uint8_t *)"\x04\x11", 2U, decoded, sizeof decoded, &n));
    /* zero inside a block */
    TEST_ASSERT_FALSE(
        cobs_decode((const uint8_t *)"\x03\x11\x00", 3U, decoded, sizeof decoded, &n));
}

static void test_decode_rejects_small_output(void)
{
    /* 11 22 00 33 */
    const uint8_t in[] = {0x03, 0x11, 0x22, 0x02, 0x33};
    TEST_ASSERT_FALSE(cobs_decode(in, sizeof in, decoded, 1U, &n)); /* data byte */
    TEST_ASSERT_FALSE(cobs_decode(in, sizeof in, decoded, 2U, &n)); /* implied zero */
    TEST_ASSERT_TRUE(cobs_decode(in, sizeof in, decoded, 4U, &n));
    TEST_ASSERT_EQUAL_size_t(4U, n);
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_reference_examples);
    UNIT_TEST(test_full_blocks);
    UNIT_TEST(test_encode_rejects_small_output);
    UNIT_TEST(test_decode_rejects_invalid_input);
    UNIT_TEST(test_decode_rejects_small_output);
    return UNITY_END();
}
