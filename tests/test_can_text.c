#include <string.h>

#include "can_text.h"
#include "test_support.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static char line[CAN_TEXT_MAX];

static void check(const can_frame_t *frame, const char *expected)
{
    size_t n = can_text_format(frame, line, sizeof line);
    TEST_ASSERT_EQUAL_STRING(expected, line);
    TEST_ASSERT_EQUAL_size_t(strlen(expected), n);
}

static void test_standard_frame(void)
{
    const can_frame_t frame = {
        .timestamp_us = 12345678U,
        .id = 0x7E8U,
        .dlc = 8U,
        .data = {0x04, 0x41, 0x0C, 0x1A, 0xF8, 0x00, 0x00, 0x00},
    };
    check(&frame, "  12.345678  7E8       [8]  04 41 0C 1A F8 00 00 00\r\n");
}

static void test_extended_frame_and_leading_zeros(void)
{
    const can_frame_t frame = {
        .timestamp_us = 1000005U, /* 1.000005 s: microseconds keep their zeros */
        .id = 0x18DAF110U,
        .dlc = 3U,
        .flags = CAN_FLAG_EXTENDED,
        .data = {0x02, 0x10, 0x03},
    };
    check(&frame, "  1.000005  18DAF110  [3]  02 10 03\r\n");
}

static void test_remote_and_empty_frames(void)
{
    const can_frame_t remote = {.timestamp_us = 0U, .id = 0x7DFU, .dlc = 8U, .flags = CAN_FLAG_RTR};
    check(&remote, "  0.000000  7DF       [8]  remote request\r\n");

    const can_frame_t empty = {.timestamp_us = 999999U, .id = 0x001U, .dlc = 0U};
    check(&empty, "  0.999999  001       [0]\r\n");
}

static void test_longest_line_fits(void)
{
    const can_frame_t frame = {
        .timestamp_us = 0xFFFFFFFFU, /* 4294.967295 s */
        .id = 0x1FFFFFFFU,
        .dlc = 8U,
        .flags = CAN_FLAG_EXTENDED,
        .data = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    };
    check(&frame, "  4294.967295  1FFFFFFF  [8]  FF FF FF FF FF FF FF FF\r\n");
    TEST_ASSERT_LESS_THAN_size_t(CAN_TEXT_MAX, strlen(line) + 1U);
}

static void test_rejects_small_buffer_and_bad_dlc(void)
{
    const can_frame_t frame = {.id = 0x123U, .dlc = 1U};
    char small[CAN_TEXT_MAX - 1U];
    TEST_ASSERT_EQUAL_size_t(0U, can_text_format(&frame, small, sizeof small));

    const can_frame_t bad = {.id = 0x123U, .dlc = 9U};
    TEST_ASSERT_EQUAL_size_t(0U, can_text_format(&bad, line, sizeof line));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_standard_frame);
    UNIT_TEST(test_extended_frame_and_leading_zeros);
    UNIT_TEST(test_remote_and_empty_frames);
    UNIT_TEST(test_longest_line_fits);
    UNIT_TEST(test_rejects_small_buffer_and_bad_dlc);
    return UNITY_END();
}
