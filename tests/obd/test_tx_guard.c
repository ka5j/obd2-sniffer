#include <stdint.h>
#include <string.h>

#include "obd/obd2.h"
#include "obd/tx_guard.h"
#include "test_support.h"

static tx_guard_t guard;
static uint32_t now_us;

void setUp(void)
{
    tx_guard_init(&guard);
    now_us = 1000000U;
}

void tearDown(void)
{
}

/* An 11-bit data frame of 8 bytes: `bytes` then padding. */
static can_frame_t frame(uint32_t id, const char *bytes, size_t len)
{
    can_frame_t f;
    (void)memset(&f, 0, sizeof f);
    f.id = id;
    f.dlc = 8U;
    (void)memset(f.data, 0x55, sizeof f.data); /* typical padding */
    (void)memcpy(f.data, bytes, len);
    return f;
}

static can_frame_t request(const char *bytes, size_t len)
{
    return frame(TX_GUARD_REQUEST_ID, bytes, len);
}

/* Each allowed request is sent a full gap after the one before. */
static bool allows(can_frame_t f)
{
    now_us += TX_GUARD_REQUEST_GAP_US;
    return tx_guard_allows(&guard, &f, now_us);
}

/* ---- what the tester sends ------------------------------------------------- */

static void test_allows_the_testers_requests(void)
{
    TEST_ASSERT_TRUE(allows(request("\x02\x09\x02", 3U))); /* VIN */
    TEST_ASSERT_TRUE(allows(request("\x01\x03", 2U)));     /* stored trouble codes */
    TEST_ASSERT_TRUE(allows(request("\x02\x01\x01", 3U))); /* monitor status */
    TEST_ASSERT_TRUE(allows(request("\x02\x01\x00", 3U))); /* supported PIDs */
    for (size_t i = 0U; i < obd2_pid_count(); i++) {
        char live[3] = {0x02, 0x01, (char)obd2_pid_at(i)->pid};
        TEST_ASSERT_TRUE(allows(request(live, sizeof live)));
    }
    TEST_ASSERT_EQUAL_UINT32(0U, guard.refused);
}

static void test_allows_flow_control_to_an_ecu(void)
{
    can_frame_t first = frame(TX_GUARD_FC_ID_FIRST, "\x30\x00\x00", 3U);
    can_frame_t last = frame(TX_GUARD_FC_ID_LAST, "\x32\x00\x00", 3U); /* overflow */
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &first, now_us));
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &last, now_us)); /* not rate limited */
}

/* ---- what it refuses ------------------------------------------------------- */

static void test_refuses_services_that_change_the_car(void)
{
    TEST_ASSERT_FALSE(allows(request("\x01\x04", 2U)));             /* clear trouble codes */
    TEST_ASSERT_FALSE(allows(request("\x02\x10\x03", 3U)));         /* diagnostic session */
    TEST_ASSERT_FALSE(allows(request("\x02\x11\x01", 3U)));         /* ECU reset */
    TEST_ASSERT_FALSE(allows(request("\x03\x2E\xF1\x90", 4U)));     /* write data */
    TEST_ASSERT_FALSE(allows(request("\x04\x31\x01\x02\x03", 5U))); /* run a routine */
    TEST_ASSERT_EQUAL_UINT32(5U, guard.refused);
}

static void test_refuses_other_pids_and_lengths(void)
{
    TEST_ASSERT_FALSE(allows(request("\x02\x01\x0A", 3U)));     /* PID not in the table */
    TEST_ASSERT_FALSE(allows(request("\x02\x09\x04", 3U)));     /* service 09, not the VIN */
    TEST_ASSERT_FALSE(allows(request("\x01\x01", 2U)));         /* 01 without a PID */
    TEST_ASSERT_FALSE(allows(request("\x02\x03\x00", 3U)));     /* 03 with a parameter */
    TEST_ASSERT_FALSE(allows(request("\x03\x01\x0C\x0D", 3U))); /* several PIDs at once */
    TEST_ASSERT_FALSE(allows(request("\x00\x01", 2U)));         /* empty single frame */
    TEST_ASSERT_FALSE(allows(request("\x10\x02\x01\x0C", 4U))); /* first frame, not single */
}

static void test_refuses_other_frames(void)
{
    can_frame_t f = request("\x02\x01\x0C", 3U);
    f.id = 0x7E0U; /* a request addressed to one ECU */
    TEST_ASSERT_FALSE(allows(f));

    f = request("\x02\x01\x0C", 3U);
    f.flags = CAN_FLAG_EXTENDED; /* 29-bit identifier */
    TEST_ASSERT_FALSE(allows(f));

    f = request("\x02\x01\x0C", 3U);
    f.flags = CAN_FLAG_RTR; /* remote frame */
    TEST_ASSERT_FALSE(allows(f));

    f = request("\x02\x01\x0C", 3U);
    f.dlc = 3U; /* not padded to 8 bytes */
    TEST_ASSERT_FALSE(allows(f));

    can_frame_t wait = frame(TX_GUARD_FC_ID_FIRST, "\x31\x00\x00", 3U); /* "wait" */
    TEST_ASSERT_FALSE(allows(wait));
    can_frame_t below = frame(TX_GUARD_FC_ID_FIRST - 1U, "\x30\x00\x00", 3U);
    TEST_ASSERT_FALSE(allows(below));
    can_frame_t above = frame(TX_GUARD_FC_ID_LAST + 1U, "\x30\x00\x00", 3U);
    TEST_ASSERT_FALSE(allows(above));
    can_frame_t test_frame = frame(0x123U, "\x00\x00\x00\x00", 4U);
    TEST_ASSERT_FALSE(allows(test_frame));
    TEST_ASSERT_EQUAL_UINT32(8U, guard.refused);
}

/* ---- rate limit ------------------------------------------------------------ */

static void test_requests_are_rate_limited(void)
{
    can_frame_t rpm = request("\x02\x01\x0C", 3U);
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &rpm, now_us)); /* the first is never held back */
    TEST_ASSERT_FALSE(tx_guard_allows(&guard, &rpm, now_us + TX_GUARD_REQUEST_GAP_US - 1U));
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &rpm, now_us + TX_GUARD_REQUEST_GAP_US));
    TEST_ASSERT_EQUAL_UINT32(1U, guard.refused);
}

static void test_a_refused_request_does_not_restart_the_gap(void)
{
    can_frame_t rpm = request("\x02\x01\x0C", 3U);
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &rpm, now_us));
    TEST_ASSERT_FALSE(tx_guard_allows(&guard, &rpm, now_us + 10000U));
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &rpm, now_us + TX_GUARD_REQUEST_GAP_US));
}

static void test_rate_limit_survives_the_clock_wrapping(void)
{
    can_frame_t rpm = request("\x02\x01\x0C", 3U);
    uint32_t before_wrap = UINT32_MAX - 10000U;
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &rpm, before_wrap));
    TEST_ASSERT_FALSE(tx_guard_allows(&guard, &rpm, 20000U)); /* 30 ms later */
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &rpm, 40001U));  /* 50 ms later */
}

static void test_flow_control_does_not_start_the_gap(void)
{
    can_frame_t fc = frame(TX_GUARD_FC_ID_FIRST, "\x30\x00\x00", 3U);
    can_frame_t rpm = request("\x02\x01\x0C", 3U);
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &rpm, now_us));
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &fc, now_us + 1000U));
    TEST_ASSERT_TRUE(tx_guard_allows(&guard, &rpm, now_us + TX_GUARD_REQUEST_GAP_US));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_allows_the_testers_requests);
    UNIT_TEST(test_allows_flow_control_to_an_ecu);
    UNIT_TEST(test_refuses_services_that_change_the_car);
    UNIT_TEST(test_refuses_other_pids_and_lengths);
    UNIT_TEST(test_refuses_other_frames);
    UNIT_TEST(test_requests_are_rate_limited);
    UNIT_TEST(test_a_refused_request_does_not_restart_the_gap);
    UNIT_TEST(test_rate_limit_survives_the_clock_wrapping);
    UNIT_TEST(test_flow_control_does_not_start_the_gap);
    return UNITY_END();
}
