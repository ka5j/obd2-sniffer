#include <stdint.h>
#include <string.h>

#include "ecu_node.h"
#include "test_support.h"

#define CC ISOTP_PADDING

static ecu_node_t node;
static uint8_t frame[ISOTP_FRAME_LEN];
static uint32_t id;

void setUp(void)
{
    ecu_node_init(&node);
}

void tearDown(void)
{
}

static void send_to_node(uint32_t to, const uint8_t *data, uint32_t now_us)
{
    ecu_node_frame(&node, to, data, ISOTP_FRAME_LEN, now_us, now_us / 1000U);
}

/* The next frame from the node, which must be due, sent at `now_us`. */
static void expect_frame(const uint8_t *expected, uint32_t now_us)
{
    TEST_ASSERT_TRUE(ecu_node_poll(&node, now_us, &id, frame));
    TEST_ASSERT_EQUAL_HEX32(ECU_NODE_REPLY_ID, id);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame, ISOTP_FRAME_LEN);
    ecu_node_sent(&node, now_us);
}

static void test_answers_a_request_to_every_ecu(void)
{
    const uint8_t request[] = {0x02, 0x01, 0x0C, CC, CC, CC, CC, CC};
    send_to_node(ECU_NODE_FUNCTIONAL_ID, request, 0U);
    const uint8_t reply[] = {0x04, 0x41, 0x0C, 0x0C, 0x80, CC, CC, CC}; /* 800 rpm */
    expect_frame(reply, 0U);
    TEST_ASSERT_FALSE(ecu_node_poll(&node, 0U, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(1U, node.answered);
}

static void test_answers_a_request_to_itself(void)
{
    const uint8_t request[] = {0x01, 0x03, CC, CC, CC, CC, CC, CC};
    send_to_node(ECU_NODE_REQUEST_ID, request, 0U);
    const uint8_t reply[] = {0x06, 0x43, 0x02, 0x03, 0x01, 0x04, 0x20, CC};
    expect_frame(reply, 0U);
}

static void test_ignores_other_identifiers(void)
{
    const uint8_t request[] = {0x02, 0x01, 0x0C, CC, CC, CC, CC, CC};
    ecu_node_frame(&node, 0x7E1U, request, ISOTP_FRAME_LEN, 0U, 0U); /* the next ECU's */
    ecu_node_frame(&node, ECU_NODE_REPLY_ID, request, ISOTP_FRAME_LEN, 0U, 0U);
    TEST_ASSERT_FALSE(ecu_node_poll(&node, 0U, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(0U, node.answered);
}

static void test_sends_the_vin_after_flow_control(void)
{
    const uint8_t request[] = {0x02, 0x09, 0x02, CC, CC, CC, CC, CC};
    send_to_node(ECU_NODE_FUNCTIONAL_ID, request, 0U);
    const uint8_t ff[] = {0x10, 0x14, 0x49, 0x02, 0x01, '1', 'M', '8'};
    TEST_ASSERT_TRUE(ecu_node_poll(&node, 0U, &id, frame));
    TEST_ASSERT_TRUE(ecu_node_poll(&node, 0U, &id, frame)); /* again, until sent */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ff, frame, ISOTP_FRAME_LEN);
    ecu_node_sent(&node, 0U);
    TEST_ASSERT_FALSE(ecu_node_poll(&node, 100U, &id, frame)); /* waits for flow control */

    const uint8_t fc[] = {0x30, 0x00, 0x00, CC, CC, CC, CC, CC};
    send_to_node(ECU_NODE_REQUEST_ID, fc, 200U);
    const uint8_t cf1[] = {0x21, 'G', 'D', 'M', '9', 'A', 'X', 'K'};
    const uint8_t cf2[] = {0x22, 'P', '0', '4', '2', '7', '8', '8'};
    expect_frame(cf1, 200U);
    expect_frame(cf2, 200U);
    TEST_ASSERT_FALSE(ecu_node_poll(&node, 200U, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(0U, node.errors);
}

static void test_silent_for_unsupported_requests(void)
{
    const uint8_t request[] = {0x02, 0x01, 0x02, CC, CC, CC, CC, CC};
    send_to_node(ECU_NODE_FUNCTIONAL_ID, request, 0U);
    TEST_ASSERT_FALSE(ecu_node_poll(&node, 0U, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(1U, node.ignored);
    TEST_ASSERT_EQUAL_UINT32(0U, node.answered);
}

static void test_counts_a_refused_or_abandoned_reply(void)
{
    const uint8_t request[] = {0x02, 0x09, 0x02, CC, CC, CC, CC, CC};
    const uint8_t ff[] = {0x10, 0x14, 0x49, 0x02, 0x01, '1', 'M', '8'};
    send_to_node(ECU_NODE_FUNCTIONAL_ID, request, 0U);
    expect_frame(ff, 0U);
    const uint8_t fc_overflow[] = {0x32, 0x00, 0x00, CC, CC, CC, CC, CC};
    send_to_node(ECU_NODE_REQUEST_ID, fc_overflow, 10U);
    TEST_ASSERT_EQUAL_UINT32(1U, node.errors);

    send_to_node(ECU_NODE_FUNCTIONAL_ID, request, 20U);
    expect_frame(ff, 20U);
    TEST_ASSERT_FALSE(ecu_node_poll(&node, 20U + 999999U, &id, frame));
    TEST_ASSERT_FALSE(ecu_node_poll(&node, 20U + 1000000U, &id, frame)); /* no flow control */
    TEST_ASSERT_EQUAL_UINT32(2U, node.errors);
}

static void test_refuses_a_request_longer_than_one_frame(void)
{
    const uint8_t ff[] = {0x10, 0x08, 0x01, 0x0C, 0x0D, 0x05, 0x11, 0x04};
    send_to_node(ECU_NODE_FUNCTIONAL_ID, ff, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, node.errors);
    const uint8_t overflow[] = {0x32, 0x00, 0x00, CC, CC, CC, CC, CC};
    expect_frame(overflow, 0U);
    TEST_ASSERT_FALSE(ecu_node_poll(&node, 0U, &id, frame));
}

static void test_sent_with_nothing_pending_does_nothing(void)
{
    ecu_node_sent(&node, 0U);
    TEST_ASSERT_FALSE(ecu_node_poll(&node, 0U, &id, frame));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_answers_a_request_to_every_ecu);
    UNIT_TEST(test_answers_a_request_to_itself);
    UNIT_TEST(test_ignores_other_identifiers);
    UNIT_TEST(test_sends_the_vin_after_flow_control);
    UNIT_TEST(test_silent_for_unsupported_requests);
    UNIT_TEST(test_counts_a_refused_or_abandoned_reply);
    UNIT_TEST(test_refuses_a_request_longer_than_one_frame);
    UNIT_TEST(test_sent_with_nothing_pending_does_nothing);
    return UNITY_END();
}
