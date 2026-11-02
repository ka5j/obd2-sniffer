#include <stdint.h>
#include <string.h>

#include "obd/ecu_node.h"
#include "obd/ecu_sim.h"
#include "obd/obd_client.h"
#include "test_support.h"

#define CC ISOTP_PADDING

static obd_client_t client;
static uint8_t frame[ISOTP_FRAME_LEN];
static uint32_t id;

void setUp(void)
{
    obd_client_init(&client);
}

void tearDown(void)
{
}

/* The client's next frame, which must be due: its identifier and bytes. */
static void expect_frame(uint32_t expected_id, const uint8_t *expected, uint32_t now_us)
{
    TEST_ASSERT_TRUE(obd_client_poll(&client, now_us, &id, frame));
    TEST_ASSERT_EQUAL_HEX32(expected_id, id);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame, ISOTP_FRAME_LEN);
    obd_client_sent(&client, now_us);
}

static void expect_request(const uint8_t *expected, uint32_t now_us)
{
    expect_frame(OBD_CLIENT_REQUEST_ID, expected, now_us);
}

static void reply(const uint8_t *data, uint32_t now_us)
{
    obd_client_frame(&client, OBD_CLIENT_REPLY_ID, data, ISOTP_FRAME_LEN, now_us);
}

static const uint8_t VIN_REQUEST[] = {0x02, 0x09, 0x02, CC, CC, CC, CC, CC};
static const uint8_t DTC_REQUEST[] = {0x01, 0x03, CC, CC, CC, CC, CC, CC};
static const uint8_t STATUS_REQUEST[] = {0x02, 0x01, 0x01, CC, CC, CC, CC, CC};
static const uint8_t RPM_REQUEST[] = {0x02, 0x01, 0x0C, CC, CC, CC, CC, CC};
static const uint8_t RPM_REPLY[] = {0x04, 0x41, 0x0C, 0x1A, 0xF8, CC, CC, CC};

/* ---- against the simulated ECU ------------------------------------------- */

static ecu_node_t node;

/* Roughly the laptop's plan (host/src/obdsniff/obdplan.py): the VIN, the
 * trouble codes and the status once, then the live values in turn. */
static void ask_next(size_t *step)
{
    if (obd_client_busy(&client)) {
        return;
    }
    size_t n = (*step < 3U) ? *step : (3U + ((*step - 3U) % OBD2_PID_COUNT));
    if (n == 0U) {
        TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_VEHICLE_INFO, OBD2_INFO_VIN));
    } else if (n == 1U) {
        TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_STORED_DTCS, 0U));
    } else if (n == 2U) {
        TEST_ASSERT_TRUE(
            obd_client_ask(&client, OBD2_SERVICE_CURRENT_DATA, OBD2_PID_MONITOR_STATUS));
    } else {
        TEST_ASSERT_TRUE(
            obd_client_ask(&client, OBD2_SERVICE_CURRENT_DATA, obd2_pid_at(n - 3U)->pid));
    }
    (*step)++;
}

/* Both nodes on one bus, the clock moving on 100 us per round, the laptop
 * asking the next question whenever the client is free. */
static void run_bus(uint32_t from_us, uint32_t to_us, size_t *step)
{
    for (uint32_t now = from_us; now < to_us; now += 100U) {
        ask_next(step);
        if (obd_client_poll(&client, now, &id, frame)) {
            obd_client_sent(&client, now);
            ecu_node_frame(&node, id, frame, ISOTP_FRAME_LEN, now, now / 1000U);
        }
        if (ecu_node_poll(&node, now, &id, frame)) {
            ecu_node_sent(&node, now);
            obd_client_frame(&client, id, frame, ISOTP_FRAME_LEN, now);
        }
    }
}

static void test_reads_everything_from_the_simulated_ecu(void)
{
    size_t step = 0U;
    ecu_node_init(&node);
    run_bus(0U, 1000000U, &step);

    TEST_ASSERT_TRUE(client.vin_valid);
    TEST_ASSERT_EQUAL_STRING(ECU_SIM_VIN, client.vin);
    TEST_ASSERT_TRUE(client.dtcs_valid);
    TEST_ASSERT_EQUAL_UINT8(2U, client.dtc_count);
    TEST_ASSERT_EQUAL_HEX16(0x0301U, client.dtcs[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0420U, client.dtcs[1]);
    TEST_ASSERT_TRUE(client.status_valid);
    TEST_ASSERT_TRUE(client.mil_on);
    TEST_ASSERT_EQUAL_UINT8(2U, client.dtc_reported);
    for (size_t i = 0U; i < OBD2_PID_COUNT; i++) {
        TEST_ASSERT_TRUE(client.value_valid[i]);
    }
    TEST_ASSERT_EQUAL_INT32(800, client.values[0]); /* idle for the first 5 s */
    TEST_ASSERT_EQUAL_UINT32(0U, client.errors);
    TEST_ASSERT_EQUAL_UINT32(0U, client.timeouts);
    TEST_ASSERT_EQUAL_UINT32(client.requests, client.replies);
    TEST_ASSERT_EQUAL_UINT32(client.requests, node.answered);
    /* However fast the laptop asks, one request per gap at most. */
    TEST_ASSERT_EQUAL_UINT32(1000000U / OBD_CLIENT_GAP_US, client.requests);

    /* Later in the drive the values follow the engine. */
    run_bus(1000000U, 8000000U, &step);
    /* Each value is read every 7 requests, 350 ms; the engine is speeding up. */
    ecu_sim_state_t earlier;
    ecu_sim_state_t now;
    ecu_sim_state(7600U, &earlier);
    ecu_sim_state(8000U, &now);
    TEST_ASSERT_TRUE(client.values[0] >= earlier.rpm - 50);
    TEST_ASSERT_TRUE(client.values[0] <= now.rpm + 50);
    TEST_ASSERT_EQUAL_UINT32(0U, client.errors);
}

/* ---- asking ----------------------------------------------------------------- */

static void test_sends_nothing_it_was_not_asked(void)
{
    TEST_ASSERT_FALSE(obd_client_busy(&client));
    TEST_ASSERT_FALSE(obd_client_poll(&client, 0U, &id, frame));
    TEST_ASSERT_FALSE(obd_client_poll(&client, 10000000U, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(0U, client.requests);
}

static void test_asks_each_kind_of_question(void)
{
    uint32_t t = 0U;
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_VEHICLE_INFO, OBD2_INFO_VIN));
    TEST_ASSERT_TRUE(obd_client_busy(&client));
    TEST_ASSERT_TRUE(obd_client_poll(&client, t, &id, frame));
    expect_request(VIN_REQUEST, t); /* handed out again until it is sent */
    TEST_ASSERT_FALSE(obd_client_poll(&client, t + OBD_CLIENT_REPLY_TIMEOUT_US - 1U, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(0U, client.timeouts);
    t += OBD_CLIENT_REPLY_TIMEOUT_US; /* no reply */
    TEST_ASSERT_FALSE(obd_client_poll(&client, t, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(1U, client.timeouts);
    TEST_ASSERT_FALSE(obd_client_busy(&client));

    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_STORED_DTCS, 0x55U)); /* PID ignored */
    expect_request(DTC_REQUEST, t);
    t += OBD_CLIENT_REPLY_TIMEOUT_US;
    (void)obd_client_poll(&client, t, &id, frame);
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_CURRENT_DATA, OBD2_PID_MONITOR_STATUS));
    expect_request(STATUS_REQUEST, t);
    t += OBD_CLIENT_REPLY_TIMEOUT_US;
    (void)obd_client_poll(&client, t, &id, frame);
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_CURRENT_DATA, OBD2_PID_ENGINE_SPEED));
    expect_request(RPM_REQUEST, t);
    TEST_ASSERT_EQUAL_UINT32(4U, client.requests);
}

static void test_refuses_unknown_questions(void)
{
    TEST_ASSERT_FALSE(obd_client_ask(&client, OBD2_SERVICE_CURRENT_DATA, 0x0BU)); /* not kept */
    TEST_ASSERT_FALSE(obd_client_ask(&client, OBD2_SERVICE_VEHICLE_INFO, 0x04U));
    TEST_ASSERT_FALSE(obd_client_ask(&client, 0x04U, 0U)); /* clear trouble codes */
    TEST_ASSERT_FALSE(obd_client_busy(&client));
}

static void test_one_question_at_a_time(void)
{
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_CURRENT_DATA, OBD2_PID_ENGINE_SPEED));
    TEST_ASSERT_FALSE(obd_client_ask(&client, OBD2_SERVICE_STORED_DTCS, 0U)); /* not yet sent */
    expect_request(RPM_REQUEST, 0U);
    TEST_ASSERT_FALSE(obd_client_ask(&client, OBD2_SERVICE_STORED_DTCS, 0U)); /* waiting */
    reply(RPM_REPLY, 100U);
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_STORED_DTCS, 0U));
}

static void test_keeps_the_gap_between_requests(void)
{
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_CURRENT_DATA, OBD2_PID_ENGINE_SPEED));
    expect_request(RPM_REQUEST, 1000U);
    reply(RPM_REPLY, 1100U);
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_CURRENT_DATA, OBD2_PID_ENGINE_SPEED));
    TEST_ASSERT_FALSE(obd_client_poll(&client, 1000U + OBD_CLIENT_GAP_US - 1U, &id, frame));
    expect_request(RPM_REQUEST, 1000U + OBD_CLIENT_GAP_US);
}

static void test_cancel_drops_the_request_and_keeps_the_answers(void)
{
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_CURRENT_DATA, OBD2_PID_ENGINE_SPEED));
    expect_request(RPM_REQUEST, 0U);
    reply(RPM_REPLY, 100U);
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_VEHICLE_INFO, OBD2_INFO_VIN));
    obd_client_cancel(&client);
    TEST_ASSERT_FALSE(obd_client_busy(&client));
    TEST_ASSERT_FALSE(obd_client_poll(&client, 1000000U, &id, frame));
    TEST_ASSERT_TRUE(client.value_valid[0]);
    TEST_ASSERT_EQUAL_UINT32(1U, client.replies);
}

/* ---- replies ---------------------------------------------------------------- */

static void test_keeps_the_values_it_is_sent(void)
{
    reply(RPM_REPLY, 0U); /* nothing was asked: ignored */
    TEST_ASSERT_FALSE(client.value_valid[0]);

    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_CURRENT_DATA, OBD2_PID_ENGINE_SPEED));
    expect_request(RPM_REQUEST, 0U);
    obd_client_frame(&client, 0x7E9U, RPM_REPLY, ISOTP_FRAME_LEN, 0U); /* another ECU's */
    TEST_ASSERT_FALSE(client.value_valid[0]);
    reply(RPM_REPLY, 100U);
    TEST_ASSERT_TRUE(client.value_valid[0]);
    TEST_ASSERT_EQUAL_INT32(1726, client.values[0]);
    TEST_ASSERT_EQUAL_UINT32(1U, client.replies);

    static const uint8_t short_rpm[] = {0x03, 0x41, 0x0C, 0x1A, CC, CC, CC, CC};
    static const uint8_t bad_status[] = {0x03, 0x41, 0x01, 0x82, CC, CC, CC, CC};
    static const uint8_t bad_dtcs[] = {0x03, 0x43, 0x02, 0x03, CC, CC, CC, CC};
    static const uint8_t bad_vin[] = {0x03, 0x49, 0x02, 0x01, CC, CC, CC, CC};
    const uint8_t *bad[] = {short_rpm, bad_status, bad_dtcs, bad_vin};
    uint32_t t = 0U;
    for (size_t i = 0U; i < 4U; i++) {
        t += OBD_CLIENT_GAP_US;
        TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_STORED_DTCS, 0U));
        expect_request(DTC_REQUEST, t);
        reply(bad[i], t + 100U);
    }
    TEST_ASSERT_FALSE(client.value_valid[0]); /* one byte short: forgotten */
    TEST_ASSERT_FALSE(client.status_valid);
    TEST_ASSERT_FALSE(client.dtcs_valid);
    TEST_ASSERT_EQUAL_UINT8(0U, client.dtc_count);
    TEST_ASSERT_FALSE(client.vin_valid);
    TEST_ASSERT_EQUAL_UINT32(4U, client.errors);
}

static void test_counts_replies_it_cannot_use(void)
{
    static const uint8_t negative[] = {0x03, 0x7F, 0x09, 0x12, CC, CC, CC, CC};
    static const uint8_t one_byte[] = {0x01, 0x41, CC, CC, CC, CC, CC, CC};
    static const uint8_t unknown_pid[] = {0x03, 0x41, 0x0B, 0x64, CC, CC, CC, CC};
    const uint8_t *replies[] = {negative, one_byte, unknown_pid};
    uint32_t t = 0U;
    for (size_t i = 0U; i < 3U; i++) {
        t += OBD_CLIENT_GAP_US;
        TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_VEHICLE_INFO, OBD2_INFO_VIN));
        expect_request(VIN_REQUEST, t);
        reply(replies[i], t + 100U);
        TEST_ASSERT_FALSE(obd_client_busy(&client)); /* the request is over */
    }
    TEST_ASSERT_EQUAL_UINT32(3U, client.errors);
    TEST_ASSERT_EQUAL_UINT32(0U, client.replies);
}

static void test_waits_for_a_reply_that_has_started(void)
{
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_VEHICLE_INFO, OBD2_INFO_VIN));
    expect_request(VIN_REQUEST, 0U);
    static const uint8_t ff[] = {0x10, 0x14, 0x49, 0x02, 0x01, '1', 'M', '8'};
    reply(ff, 40000U);
    static const uint8_t fc[] = {0x30, 0x00, 0x00, CC, CC, CC, CC, CC};
    TEST_ASSERT_TRUE(obd_client_poll(&client, 40000U, &id, frame));
    TEST_ASSERT_TRUE(obd_client_poll(&client, 40000U, &id, frame)); /* again, until sent */
    TEST_ASSERT_EQUAL_HEX32(OBD_CLIENT_FC_ID, id);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fc, frame, ISOTP_FRAME_LEN);
    obd_client_sent(&client, 40000U);

    /* Past the 50 ms reply limit, but the reply has begun: no timeout. */
    TEST_ASSERT_FALSE(obd_client_poll(&client, 60000U, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(0U, client.timeouts);
    /* The rest never comes: ISO-TP gives up after its own limit. */
    TEST_ASSERT_FALSE(obd_client_poll(&client, 40000U + 1000000U, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(1U, client.errors);
    TEST_ASSERT_FALSE(obd_client_busy(&client));
}

static void test_reads_a_multi_frame_vin(void)
{
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_VEHICLE_INFO, OBD2_INFO_VIN));
    expect_request(VIN_REQUEST, 0U);
    static const uint8_t ff[] = {0x10, 0x14, 0x49, 0x02, 0x01, '1', 'M', '8'};
    static const uint8_t cf1[] = {0x21, 'G', 'D', 'M', '9', 'A', 'X', 'K'};
    static const uint8_t cf2[] = {0x22, 'P', '0', '4', '2', '7', '8', '8'};
    static const uint8_t fc[] = {0x30, 0x00, 0x00, CC, CC, CC, CC, CC};
    reply(ff, 100U);
    expect_frame(OBD_CLIENT_FC_ID, fc, 100U);
    reply(cf1, 200U);
    reply(cf2, 300U);
    TEST_ASSERT_TRUE(client.vin_valid);
    TEST_ASSERT_EQUAL_STRING("1M8GDM9AXKP042788", client.vin);
    TEST_ASSERT_FALSE(obd_client_busy(&client));
}

static void test_a_broken_reply_ends_the_request(void)
{
    TEST_ASSERT_TRUE(obd_client_ask(&client, OBD2_SERVICE_VEHICLE_INFO, OBD2_INFO_VIN));
    expect_request(VIN_REQUEST, 0U);
    static const uint8_t ff[] = {0x10, 0x14, 0x49, 0x02, 0x01, '1', 'M', '8'};
    reply(ff, 100U);
    TEST_ASSERT_TRUE(obd_client_poll(&client, 100U, &id, frame));
    obd_client_sent(&client, 100U);
    static const uint8_t cf2[] = {0x22, 'P', '0', '4', '2', '7', '8', '8'};
    reply(cf2, 200U); /* CF 1 missing */
    TEST_ASSERT_EQUAL_UINT32(1U, client.errors);
    TEST_ASSERT_FALSE(obd_client_busy(&client));
}

static void test_sent_with_nothing_pending_does_nothing(void)
{
    obd_client_sent(&client, 0U);
    TEST_ASSERT_EQUAL_UINT32(0U, client.requests);
    TEST_ASSERT_FALSE(obd_client_busy(&client));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_reads_everything_from_the_simulated_ecu);
    UNIT_TEST(test_sends_nothing_it_was_not_asked);
    UNIT_TEST(test_asks_each_kind_of_question);
    UNIT_TEST(test_refuses_unknown_questions);
    UNIT_TEST(test_one_question_at_a_time);
    UNIT_TEST(test_keeps_the_gap_between_requests);
    UNIT_TEST(test_cancel_drops_the_request_and_keeps_the_answers);
    UNIT_TEST(test_keeps_the_values_it_is_sent);
    UNIT_TEST(test_counts_replies_it_cannot_use);
    UNIT_TEST(test_waits_for_a_reply_that_has_started);
    UNIT_TEST(test_reads_a_multi_frame_vin);
    UNIT_TEST(test_a_broken_reply_ends_the_request);
    UNIT_TEST(test_sent_with_nothing_pending_does_nothing);
    return UNITY_END();
}
