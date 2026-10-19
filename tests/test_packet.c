#include <stdint.h>
#include <string.h>

#include "packet.h"
#include "test_support.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* Golden vectors, computed with an independent model of the format. The same
 * bytes are checked by the laptop tool's decoder tests
 * (host/tests/test_packets.py), so both sides agree on the format. */

/* An OBD2 engine-speed reply: ID 7E8, 8 bytes, timestamp 0x12345678 us,
 * packet number 0x0102. Its trailing zero bytes exercise the COBS stuffing. */
static const uint8_t GOLDEN_STANDARD[] = {
    0x00, 0x0A, 0x01, 0x02, 0x01, 0x78, 0x56, 0x34, 0x12, 0xE8, 0x07, 0x01, 0x07,
    0x08, 0x04, 0x41, 0x0C, 0x1A, 0xF8, 0x01, 0x01, 0x03, 0x3D, 0x84, 0x00,
};

/* A remote request with a 29-bit ID: no data bytes on the wire. */
static const uint8_t GOLDEN_EXTENDED_REMOTE[] = {
    0x00, 0x04, 0x01, 0xFF, 0xFF, 0x01, 0x01, 0x01, 0x08,
    0x10, 0xF1, 0xDA, 0xD8, 0x08, 0xF3, 0x01, 0x00,
};

static uint8_t out[64];

static void test_standard_frame_matches_golden_vector(void)
{
    const can_frame_t frame = {
        .timestamp_us = 0x12345678U,
        .id = 0x7E8U,
        .dlc = 8U,
        .data = {0x04, 0x41, 0x0C, 0x1A, 0xF8, 0x00, 0x00, 0x00},
        .seq = 0x0102U,
    };
    size_t n = packet_encode_can_frame(&frame, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(sizeof GOLDEN_STANDARD, n);
    TEST_ASSERT_EQUAL_MEMORY(GOLDEN_STANDARD, out, n);
    TEST_ASSERT_EQUAL_size_t(PACKET_CAN_FRAME_MAX, n); /* the largest a frame gets */
}

static void test_extended_remote_frame_matches_golden_vector(void)
{
    const can_frame_t frame = {
        .timestamp_us = 0U,
        .id = 0x18DAF110U,
        .dlc = 8U,
        .flags = CAN_FLAG_EXTENDED | CAN_FLAG_RTR,
        .data = {1, 2, 3, 4, 5, 6, 7, 8}, /* ignored: a remote frame has no data */
        .seq = 0xFFFFU,
    };
    size_t n = packet_encode_can_frame(&frame, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(sizeof GOLDEN_EXTENDED_REMOTE, n);
    TEST_ASSERT_EQUAL_MEMORY(GOLDEN_EXTENDED_REMOTE, out, n);
}

static void test_packet_has_zeros_only_at_its_ends(void)
{
    const can_frame_t frame = {.id = 0U, .dlc = 8U}; /* all-zero data */
    size_t n = packet_encode_can_frame(&frame, out, sizeof out);
    TEST_ASSERT_GREATER_THAN_size_t(2U, n);
    TEST_ASSERT_EQUAL_HEX8(PACKET_DELIMITER, out[0]);
    TEST_ASSERT_EQUAL_HEX8(PACKET_DELIMITER, out[n - 1U]);
    for (size_t i = 1U; i < (n - 1U); i++) {
        TEST_ASSERT_NOT_EQUAL(0, out[i]);
    }
}

static void test_rejects_bad_dlc_and_small_buffer(void)
{
    const can_frame_t bad = {.id = 0x123U, .dlc = 9U};
    TEST_ASSERT_EQUAL_size_t(0U, packet_encode_can_frame(&bad, out, sizeof out));

    const can_frame_t frame = {.id = 0x123U, .dlc = 8U, .data = {1, 2, 3, 4, 5, 6, 7, 8}};
    TEST_ASSERT_EQUAL_size_t(0U, packet_encode_can_frame(&frame, out, 1U));
    TEST_ASSERT_EQUAL_size_t(0U, packet_encode_can_frame(&frame, out, 10U)); /* body too big */
    TEST_ASSERT_EQUAL_size_t(PACKET_CAN_FRAME_MAX,
                             packet_encode_can_frame(&frame, out, PACKET_CAN_FRAME_MAX));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_standard_frame_matches_golden_vector);
    UNIT_TEST(test_extended_remote_frame_matches_golden_vector);
    UNIT_TEST(test_packet_has_zeros_only_at_its_ends);
    UNIT_TEST(test_rejects_bad_dlc_and_small_buffer);
    return UNITY_END();
}
