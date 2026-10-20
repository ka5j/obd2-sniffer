#include <stdint.h>
#include <string.h>

#include "isotp.h"
#include "test_support.h"

#define TIMEOUT_US 1000000U /* 1 s */
#define CC         ISOTP_PADDING

static isotp_rx_t rx;
static isotp_tx_t tx;
static uint8_t rx_buf[ISOTP_MESSAGE_MAX];
static uint8_t frame[ISOTP_FRAME_LEN];
static uint8_t message[ISOTP_MESSAGE_MAX];

void setUp(void)
{
    isotp_rx_init(&rx, rx_buf, sizeof rx_buf, 0U, 0U, TIMEOUT_US);
    isotp_tx_init(&tx, TIMEOUT_US);
    for (uint32_t i = 0U; i < sizeof message; i++) {
        message[i] = (uint8_t)(i * 7U);
    }
}

void tearDown(void)
{
}

/* Feed an 8-byte frame to the receiver. */
static isotp_result_t rx_feed(const uint8_t *data, uint32_t now)
{
    return isotp_rx_frame(&rx, data, ISOTP_FRAME_LEN, now);
}

/* Feed an 8-byte frame to the sender. */
static isotp_result_t tx_feed(const uint8_t *data, uint32_t now)
{
    return isotp_tx_frame(&tx, data, ISOTP_FRAME_LEN, now);
}

/* The receiver's flow control, sent at `now`. */
static void rx_send_fc(uint32_t now)
{
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_rx_poll(&rx, now, frame));
    isotp_rx_sent(&rx, now);
}

/* A first frame for a `length`-byte message, accepted, with its flow control sent. */
static void rx_start_message(uint16_t length, uint32_t now)
{
    const uint8_t ff[] = {(uint8_t)(0x10U | (length >> 8U)), (uint8_t)length, 1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(ff, now));
    rx_send_fc(now);
}

/* Start sending `length` bytes of `message` and confirm the first frame at `now`. */
static void tx_start_message(uint16_t length, uint32_t now)
{
    TEST_ASSERT_TRUE(isotp_tx_start(&tx, message, length));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, now, frame));
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_sent(&tx, now));
}

/* Run a whole transfer from `tx` to `rx` over a perfect link, the clock
 * moving on `step_us` per round. Returns the receiver's final result. */
static isotp_result_t transfer(uint32_t *now, uint32_t step_us)
{
    for (uint32_t round = 0U; round < 100000U; round++) {
        if (isotp_tx_poll(&tx, *now, frame) == ISOTP_SEND) {
            (void)isotp_tx_sent(&tx, *now);
            isotp_result_t result = rx_feed(frame, *now);
            if (result != ISOTP_OK) {
                return result;
            }
        }
        if (isotp_rx_poll(&rx, *now, frame) == ISOTP_SEND) {
            isotp_rx_sent(&rx, *now);
            TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(frame, *now));
        }
        *now += step_us;
    }
    return ISOTP_ERR_TIMEOUT;
}

/* ---- both sides together --------------------------------------------------- */

static void test_single_frame_round_trip(void)
{
    const uint8_t request[] = {0x01, 0x0C}; /* OBD2: current engine speed */
    TEST_ASSERT_TRUE(isotp_tx_start(&tx, request, sizeof request));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, 0U, frame));
    const uint8_t expected[] = {0x02, 0x01, 0x0C, CC, CC, CC, CC, CC};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame, ISOTP_FRAME_LEN);
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, isotp_tx_sent(&tx, 0U));
    TEST_ASSERT_FALSE(isotp_tx_busy(&tx));

    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, rx_feed(frame, 0U));
    TEST_ASSERT_EQUAL_UINT16(2U, rx.length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(request, rx_buf, 2U);
}

/* The 20-byte VIN reply (service 09 PID 02), frame by frame as on the bus. */
static void test_vin_reply_frame_by_frame(void)
{
    const uint8_t vin[] = {0x49, 0x02, 0x01, '1', 'G', '1', 'J', 'C', '5', '4',
                           '4',  '4',  'R',  '7', '2', '5', '2', '3', '6', '7'};
    const uint8_t ff[] = {0x10, 0x14, 0x49, 0x02, 0x01, '1', 'G', '1'};
    const uint8_t fc[] = {0x30, 0x00, 0x00, CC, CC, CC, CC, CC};
    const uint8_t cf1[] = {0x21, 'J', 'C', '5', '4', '4', '4', 'R'};
    const uint8_t cf2[] = {0x22, '7', '2', '5', '2', '3', '6', '7'};

    TEST_ASSERT_TRUE(isotp_tx_start(&tx, vin, sizeof vin));
    TEST_ASSERT_TRUE(isotp_tx_busy(&tx));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, 0U, frame));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ff, frame, ISOTP_FRAME_LEN);
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_sent(&tx, 0U));
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_poll(&tx, 10U, frame)); /* waits for flow control */

    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(ff, 0U));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_rx_poll(&rx, 0U, frame));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fc, frame, ISOTP_FRAME_LEN);
    isotp_rx_sent(&rx, 0U);

    TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(fc, 20U));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, 20U, frame));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(cf1, frame, ISOTP_FRAME_LEN);
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_sent(&tx, 20U));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, 20U, frame));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(cf2, frame, ISOTP_FRAME_LEN);
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, isotp_tx_sent(&tx, 20U));

    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf1, 30U));
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, rx_feed(cf2, 30U));
    TEST_ASSERT_EQUAL_UINT16(sizeof vin, rx.length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(vin, rx_buf, sizeof vin);
}

/* Every size class, block sizes that do and don't divide the frame count,
 * gaps between frames, and sequence numbers wrapping past 15. */
static void test_round_trips_of_every_size(void)
{
    const uint16_t lengths[] = {1U, 7U, 8U, 13U, 14U, 15U, 111U, 112U, ISOTP_MESSAGE_MAX};
    const uint8_t block_sizes[] = {0U, 1U, 3U};
    const uint8_t gaps[] = {0x00U, 0x01U, 0xF1U};
    uint32_t now = 0xFFFF0000U; /* the clock wraps during the run */
    for (size_t l = 0U; l < (sizeof lengths / sizeof lengths[0]); l++) {
        for (size_t b = 0U; b < sizeof block_sizes; b++) {
            for (size_t g = 0U; g < sizeof gaps; g++) {
                isotp_rx_init(&rx, rx_buf, sizeof rx_buf, block_sizes[b], gaps[g], TIMEOUT_US);
                (void)memset(rx_buf, 0, sizeof rx_buf);
                TEST_ASSERT_TRUE(isotp_tx_start(&tx, message, lengths[l]));
                TEST_ASSERT_EQUAL_INT(ISOTP_DONE, transfer(&now, 50U));
                TEST_ASSERT_FALSE(isotp_tx_busy(&tx));
                TEST_ASSERT_EQUAL_UINT16(lengths[l], rx.length);
                TEST_ASSERT_EQUAL_HEX8_ARRAY(message, rx_buf, lengths[l]);
            }
        }
    }
}

/* ---- receiver -------------------------------------------------------------- */

static void test_receiver_ignores_malformed_frames(void)
{
    const uint8_t sf_empty[] = {0x00, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t sf_seven[] = {0x07, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t ff_short[] = {0x10, 0x07, 1, 2, 3, 4, 5, 6}; /* 7 bytes: a single frame's job */
    const uint8_t cf[] = {0x21, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t fc[] = {0x30, 0, 0, CC, CC, CC, CC, CC};
    const uint8_t reserved[] = {0x40, 1, 2, 3, 4, 5, 6, 7};

    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_frame(&rx, sf_seven, 0U, 0U)); /* no bytes */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(sf_empty, 0U));                 /* length 0 */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_frame(&rx, sf_seven, 7U, 0U)); /* too short */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_frame(&rx, ff_short, 7U, 0U)); /* FF not full */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(ff_short, 0U));                 /* FF of 7 bytes */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf, 0U));                       /* CF, no message */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(fc, 0U));                       /* for the sender */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(reserved, 0U));                 /* unknown kind */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_poll(&rx, 5U * TIMEOUT_US, frame)); /* still idle */

    /* A DLC above 8 means 8 bytes. */
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, isotp_rx_frame(&rx, sf_seven, 15U, 0U));
    TEST_ASSERT_EQUAL_UINT16(7U, rx.length);
}

static void test_receiver_accepts_frames_without_padding(void)
{
    const uint8_t sf[] = {0x03, 0x41, 0x0D, 0x32};
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, isotp_rx_frame(&rx, sf, sizeof sf, 0U));
    TEST_ASSERT_EQUAL_UINT16(3U, rx.length);

    /* 8 bytes: the first frame carries 6, the last CF needs only 3 bytes on the bus. */
    rx_start_message(8U, 0U);
    const uint8_t cf_too_short[] = {0x21, 7};
    const uint8_t cf_last[] = {0x21, 7, 8};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_frame(&rx, cf_too_short, sizeof cf_too_short, 0U));
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, isotp_rx_frame(&rx, cf_last, sizeof cf_last, 0U));
    const uint8_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, rx_buf, sizeof expected);
}

static void test_receiver_ignores_a_short_full_consecutive_frame(void)
{
    rx_start_message(20U, 0U);
    const uint8_t cf[] = {0x21, 7, 8, 9, 10, 11, 12, 13};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_frame(&rx, cf, 7U, 0U)); /* needs all 8 bytes */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf, 0U));                 /* still expected */
    TEST_ASSERT_EQUAL_UINT16(13U, rx.received);
}

static void test_receiver_overflow(void)
{
    uint8_t small[10];
    isotp_rx_init(&rx, small, sizeof small, 0U, 0U, TIMEOUT_US);

    const uint8_t sf_fits[] = {0x07, 1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, rx_feed(sf_fits, 0U));

    const uint8_t ff_too_long[] = {0x10, 0x0B, 1, 2, 3, 4, 5, 6}; /* 11 bytes */
    TEST_ASSERT_EQUAL_INT(ISOTP_ERR_OVERFLOW, rx_feed(ff_too_long, 0U));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_rx_poll(&rx, 0U, frame));
    const uint8_t overflow[] = {0x32, 0x00, 0x00, CC, CC, CC, CC, CC};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(overflow, frame, ISOTP_FRAME_LEN);
    isotp_rx_sent(&rx, 0U);
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_poll(&rx, 0U, frame)); /* back to idle */

    const uint8_t ff_fits[] = {0x10, 0x0A, 1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(ff_fits, 0U));

    uint8_t tiny[4];
    isotp_rx_init(&rx, tiny, sizeof tiny, 0U, 0U, TIMEOUT_US);
    TEST_ASSERT_EQUAL_INT(ISOTP_ERR_OVERFLOW, rx_feed(sf_fits, 0U)); /* no flow control for an SF */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_poll(&rx, 0U, frame));
}

static void test_receiver_sequence_error(void)
{
    rx_start_message(20U, 0U);
    const uint8_t cf1[] = {0x21, 7, 8, 9, 10, 11, 12, 13};
    const uint8_t cf3[] = {0x23, 14, 15, 16, 17, 18, 19, 20};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf1, 0U));
    TEST_ASSERT_EQUAL_INT(ISOTP_ERR_SEQUENCE, rx_feed(cf3, 0U)); /* CF 2 went missing */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf1, 0U));           /* message abandoned */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_poll(&rx, 5U * TIMEOUT_US, frame));
}

static void test_receiver_times_out_between_frames(void)
{
    const uint32_t start = 1000U;
    rx_start_message(20U, start);
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_poll(&rx, start + TIMEOUT_US - 1U, frame));

    const uint8_t cf1[] = {0x21, 7, 8, 9, 10, 11, 12, 13};
    const uint32_t later = start + TIMEOUT_US - 1U;
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf1, later)); /* each CF restarts the wait */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_poll(&rx, later + TIMEOUT_US - 1U, frame));
    TEST_ASSERT_EQUAL_INT(ISOTP_ERR_TIMEOUT, isotp_rx_poll(&rx, later + TIMEOUT_US, frame));
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_poll(&rx, later + TIMEOUT_US, frame));
}

static void test_receiver_asks_for_each_block(void)
{
    isotp_rx_init(&rx, rx_buf, sizeof rx_buf, 2U, 0x05U, TIMEOUT_US);
    const uint8_t ff[] = {0x10, 0x1B, 1, 2, 3, 4, 5, 6}; /* 27 bytes: FF + 3 CFs */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(ff, 0U));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_rx_poll(&rx, 0U, frame));
    const uint8_t fc[] = {0x30, 0x02, 0x05, CC, CC, CC, CC, CC};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fc, frame, ISOTP_FRAME_LEN);
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_rx_poll(&rx, 0U, frame)); /* until confirmed */
    isotp_rx_sent(&rx, 0U);

    const uint8_t cf1[] = {0x21, 7, 8, 9, 10, 11, 12, 13};
    const uint8_t cf2[] = {0x22, 14, 15, 16, 17, 18, 19, 20};
    const uint8_t cf3[] = {0x23, 21, 22, 23, 24, 25, 26, 27};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf1, 0U));
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_poll(&rx, 0U, frame));
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf2, 0U));
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf3, 0U)); /* early: the next FC hasn't gone yet */
    rx_send_fc(0U);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fc, frame, ISOTP_FRAME_LEN);
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, rx_feed(cf3, 0U));
    TEST_ASSERT_EQUAL_UINT16(27U, rx.length);
}

static void test_receiver_restarts_on_a_new_message(void)
{
    rx_start_message(20U, 0U);
    const uint8_t sf[] = {0x02, 0x41, 0x00, CC, CC, CC, CC, CC};
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, rx_feed(sf, 0U)); /* replaces the unfinished one */
    TEST_ASSERT_EQUAL_UINT16(2U, rx.length);
    const uint8_t cf1[] = {0x21, 7, 8, 9, 10, 11, 12, 13};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf1, 0U)); /* ignored now */

    rx_start_message(20U, 0U);
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(cf1, 0U));
    const uint8_t ff[] = {0x10, 0x09, 9, 9, 9, 9, 9, 9};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, rx_feed(ff, 0U)); /* starts over */
    rx_send_fc(0U);
    const uint8_t last[] = {0x21, 9, 9, 9, CC, CC, CC, CC};
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, rx_feed(last, 0U));
    TEST_ASSERT_EQUAL_UINT16(9U, rx.length);
}

static void test_receiver_sent_with_nothing_pending_does_nothing(void)
{
    isotp_rx_sent(&rx, 0U);
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_rx_poll(&rx, 5U * TIMEOUT_US, frame));
}

/* ---- sender ---------------------------------------------------------------- */

static void test_sender_length_limits(void)
{
    TEST_ASSERT_FALSE(isotp_tx_start(&tx, message, 0U));
    TEST_ASSERT_FALSE(isotp_tx_start(&tx, message, ISOTP_MESSAGE_MAX + 1U));
    TEST_ASSERT_FALSE(isotp_tx_busy(&tx));
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_poll(&tx, 0U, frame)); /* nothing to send */

    TEST_ASSERT_TRUE(isotp_tx_start(&tx, message, ISOTP_MESSAGE_MAX));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, 0U, frame));
    TEST_ASSERT_EQUAL_HEX8(0x1F, frame[0]); /* first frame, length 0xFFF */
    TEST_ASSERT_EQUAL_HEX8(0xFF, frame[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(message, &frame[2], 6U);
}

static void test_sender_repeats_a_frame_until_it_is_sent(void)
{
    TEST_ASSERT_TRUE(isotp_tx_start(&tx, message, 3U));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, 0U, frame));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, 1U, frame)); /* mailbox was full */
    TEST_ASSERT_EQUAL_HEX8(0x03, frame[0]);
    TEST_ASSERT_EQUAL_INT(ISOTP_DONE, isotp_tx_sent(&tx, 1U));
}

static void test_sender_times_out_without_flow_control(void)
{
    tx_start_message(20U, 500U);
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_poll(&tx, 500U + TIMEOUT_US - 1U, frame));
    TEST_ASSERT_EQUAL_INT(ISOTP_ERR_TIMEOUT, isotp_tx_poll(&tx, 500U + TIMEOUT_US, frame));
    TEST_ASSERT_FALSE(isotp_tx_busy(&tx));
}

static void test_sender_ignores_frames_it_does_not_expect(void)
{
    const uint8_t fc[] = {0x30, 0, 0, CC, CC, CC, CC, CC};
    const uint8_t cf[] = {0x21, 1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(fc, 0U)); /* idle */

    tx_start_message(20U, 0U);
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_frame(&tx, fc, 2U, 0U)); /* too short */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(cf, 0U));                 /* not flow control */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_poll(&tx, 0U, frame));   /* still waiting */

    TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(fc, 0U));
    const uint8_t fc_overflow[] = {0x32, 0, 0, CC, CC, CC, CC, CC};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(fc_overflow, 0U)); /* sending: FC not expected */
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, 0U, frame));
}

static void test_sender_waits_when_asked_and_gives_up_eventually(void)
{
    const uint8_t fc_wait[] = {0x31, 0, 0, CC, CC, CC, CC, CC};
    const uint8_t fc_go[] = {0x30, 1, 0, CC, CC, CC, CC, CC}; /* block size 1 */
    uint32_t now = 0U;
    tx_start_message(30U, now);
    for (uint32_t i = 0U; i < ISOTP_MAX_WAITS; i++) {
        now += TIMEOUT_US - 1U;
        TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(fc_wait, now)); /* each wait restarts the clock */
        TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_poll(&tx, now + TIMEOUT_US - 1U, frame));
    }
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(fc_go, now)); /* "continue" clears the count */
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, now, frame));
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_sent(&tx, now)); /* end of the block */
    for (uint32_t i = 0U; i < ISOTP_MAX_WAITS; i++) {
        TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(fc_wait, now));
    }
    TEST_ASSERT_EQUAL_INT(ISOTP_ERR_PROTOCOL, tx_feed(fc_wait, now));
    TEST_ASSERT_FALSE(isotp_tx_busy(&tx));
}

static void test_sender_stops_on_overflow_or_invalid_flow_status(void)
{
    const uint8_t fc_overflow[] = {0x32, 0, 0, CC, CC, CC, CC, CC};
    const uint8_t fc_invalid[] = {0x33, 0, 0, CC, CC, CC, CC, CC};
    tx_start_message(20U, 0U);
    TEST_ASSERT_EQUAL_INT(ISOTP_ERR_OVERFLOW, tx_feed(fc_overflow, 0U));
    TEST_ASSERT_FALSE(isotp_tx_busy(&tx));
    tx_start_message(20U, 0U);
    TEST_ASSERT_EQUAL_INT(ISOTP_ERR_PROTOCOL, tx_feed(fc_invalid, 0U));
    TEST_ASSERT_FALSE(isotp_tx_busy(&tx));
}

/* After each consecutive frame the sender waits STmin before the next. */
static void check_gap(uint8_t st_min, uint32_t expected_us)
{
    const uint8_t fc[] = {0x30, 0, st_min, CC, CC, CC, CC, CC};
    const uint32_t t = 100U;
    tx_start_message(30U, 0U);
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(fc, t));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, t, frame)); /* the first CF at once */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_sent(&tx, t));
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_poll(&tx, t + expected_us - 1U, frame));
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, t + expected_us, frame));
    TEST_ASSERT_EQUAL_HEX8(0x22, frame[0]);
}

static void test_sender_keeps_the_requested_gap(void)
{
    check_gap(0x05U, 5000U); /* milliseconds */
    check_gap(0x7FU, 127000U);
    check_gap(0xF1U, 100U); /* hundreds of microseconds */
    check_gap(0xF9U, 900U);
    check_gap(0x80U, 127000U); /* reserved values: the longest valid gap */
    check_gap(0xF0U, 127000U);
    check_gap(0xFAU, 127000U);
}

static void test_sender_waits_for_flow_control_after_each_block(void)
{
    const uint8_t fc[] = {0x30, 2, 0, CC, CC, CC, CC, CC};
    tx_start_message(30U, 0U); /* FF + 4 CFs */
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(fc, 0U));
    for (uint32_t cf = 0U; cf < 2U; cf++) {
        TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, 0U, frame));
        TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_sent(&tx, 0U));
    }
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_poll(&tx, 0U, frame)); /* waits */
    TEST_ASSERT_EQUAL_INT(ISOTP_ERR_TIMEOUT, isotp_tx_poll(&tx, TIMEOUT_US, frame));
}

static void test_sender_sent_with_nothing_pending_does_nothing(void)
{
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_sent(&tx, 0U)); /* idle */
    tx_start_message(20U, 0U);
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_sent(&tx, 0U)); /* waiting for flow control */
    const uint8_t fc[] = {0x30, 0, 0x7F, CC, CC, CC, CC, CC};
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, tx_feed(fc, 0U));
    TEST_ASSERT_EQUAL_INT(ISOTP_OK, isotp_tx_sent(&tx, 0U)); /* no frame handed out yet */
    TEST_ASSERT_EQUAL_INT(ISOTP_SEND, isotp_tx_poll(&tx, 0U, frame));
    TEST_ASSERT_EQUAL_HEX8(0x21, frame[0]); /* still the first CF */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&message[6], &frame[1], 7U);
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_single_frame_round_trip);
    UNIT_TEST(test_vin_reply_frame_by_frame);
    UNIT_TEST(test_round_trips_of_every_size);
    UNIT_TEST(test_receiver_ignores_malformed_frames);
    UNIT_TEST(test_receiver_accepts_frames_without_padding);
    UNIT_TEST(test_receiver_ignores_a_short_full_consecutive_frame);
    UNIT_TEST(test_receiver_overflow);
    UNIT_TEST(test_receiver_sequence_error);
    UNIT_TEST(test_receiver_times_out_between_frames);
    UNIT_TEST(test_receiver_asks_for_each_block);
    UNIT_TEST(test_receiver_restarts_on_a_new_message);
    UNIT_TEST(test_receiver_sent_with_nothing_pending_does_nothing);
    UNIT_TEST(test_sender_length_limits);
    UNIT_TEST(test_sender_repeats_a_frame_until_it_is_sent);
    UNIT_TEST(test_sender_times_out_without_flow_control);
    UNIT_TEST(test_sender_ignores_frames_it_does_not_expect);
    UNIT_TEST(test_sender_waits_when_asked_and_gives_up_eventually);
    UNIT_TEST(test_sender_stops_on_overflow_or_invalid_flow_status);
    UNIT_TEST(test_sender_keeps_the_requested_gap);
    UNIT_TEST(test_sender_waits_for_flow_control_after_each_block);
    UNIT_TEST(test_sender_sent_with_nothing_pending_does_nothing);
    return UNITY_END();
}
