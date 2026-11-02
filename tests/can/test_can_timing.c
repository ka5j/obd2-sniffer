#include <stdint.h>

#include "can/can_timing.h"
#include "test_support.h"

#define APB1_HZ 42000000U /* the board's CAN clock */

void setUp(void)
{
}

void tearDown(void)
{
}

static can_timing_t t;

static void check(uint32_t clock_hz, uint32_t bitrate, uint32_t prescaler, uint32_t bs1,
                  uint32_t bs2, uint32_t sample_point)
{
    TEST_ASSERT_TRUE(can_timing_calc(clock_hz, bitrate, &t));
    TEST_ASSERT_EQUAL_UINT32(prescaler, t.prescaler);
    TEST_ASSERT_EQUAL_UINT32(bs1, t.bs1);
    TEST_ASSERT_EQUAL_UINT32(bs2, t.bs2);
    TEST_ASSERT_EQUAL_UINT32(bs2, t.sjw);
    TEST_ASSERT_EQUAL_UINT32(sample_point, can_timing_sample_point(&t));
    /* exact: the bit rate comes out with no error */
    TEST_ASSERT_EQUAL_UINT32(clock_hz, t.prescaler * can_timing_tq_per_bit(&t) * bitrate);
}

/* The rates cars and diagnostic tools use, from the board's 42 MHz clock.
 * 500k: 12, 14 and 21 quanta divide exactly; 14 gives 85.7%, the closest to
 * 87.5% (12 gives 83.3% or 91.7%; 21 would need segment 1 of 17, limit 16). */
static void test_board_bit_rates(void)
{
    check(APB1_HZ, 1000000U, 3U, 11U, 2U, 857U);
    check(APB1_HZ, 500000U, 6U, 11U, 2U, 857U);
    /* 250k: 8 quanta divide exactly and sample at exactly 87.5%. */
    check(APB1_HZ, 250000U, 21U, 6U, 1U, 875U);
    /* 125k: 8 and 16 quanta both give 87.5%; the finer 16 is kept. */
    check(APB1_HZ, 125000U, 21U, 13U, 2U, 875U);
}

static void test_ties_keep_more_quanta(void)
{
    /* 48 MHz / 500k = 96: 8 quanta (prescaler 12) and 16 quanta (prescaler 6)
     * both sample at exactly 87.5%; the finer 16 is kept. */
    check(48000000U, 500000U, 6U, 13U, 2U, 875U);
}

static void test_segment_limits_respected(void)
{
    /* 40 MHz / 500k = 80: 20 quanta would need segment 1 of 17 (above the
     * limit of 16), so 16 quanta at 87.5% is chosen. */
    check(40000000U, 500000U, 5U, 13U, 2U, 875U);
}

static void test_impossible_rates_rejected(void)
{
    TEST_ASSERT_FALSE(can_timing_calc(APB1_HZ, 0U, &t));
    TEST_ASSERT_FALSE(can_timing_calc(APB1_HZ, 6000000U, &t)); /* fewer than 8 quanta */
    TEST_ASSERT_FALSE(can_timing_calc(0U, 500000U, &t));
    TEST_ASSERT_FALSE(can_timing_calc(APB1_HZ, 1000U, &t));  /* prescaler above 1024 */
    TEST_ASSERT_FALSE(can_timing_calc(APB1_HZ, 33333U, &t)); /* no exact divisor */
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_board_bit_rates);
    UNIT_TEST(test_ties_keep_more_quanta);
    UNIT_TEST(test_segment_limits_respected);
    UNIT_TEST(test_impossible_rates_rejected);
    return UNITY_END();
}
