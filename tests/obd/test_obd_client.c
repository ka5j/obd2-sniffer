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

/* Both nodes on one bus, the clock moving on 100 us per round. */
static void run_bus(uint32_t from_us, uint32_t to_us)
{
    for (uint32_t now = from_us; now < to_us; now += 100U) {
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
    ecu_node_init(&node);
    obd_client_start(&client, 0U);
    run_bus(0U, 1000000U);

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
    /* Each request and its reply take a few rounds, then the 10 ms gap. */
    TEST_ASSERT_UINT32_WITHIN(5U, 1000000U / OBD_CLIENT_GAP_US, client.requests);

    /* Later in the drive the values follow the engine. */
    run_bus(1000000U, 8000000U);
    ecu_sim_state_t engine;
    ecu_sim_state(8000U, &engine);
    TEST_ASSERT_INT32_WITHIN(100, engine.rpm, client.values[0]);
    TEST_ASSERT_EQUAL_UINT32(0U, client.errors);
}

/* ---- request plan ----------------------------------------------------------- */

static void test_asks_vin_codes_status_then_each_pid(void)
{
    obd_client_start(&client, 0U);
    TEST_ASSERT_TRUE(obd_client_running(&client));
    uint32_t t = 0U;
    TEST_ASSERT_TRUE(obd_client_poll(&client, t, &id, frame));
    expect_request(VIN_REQUEST, t); /* handed out again until it is sent */
    TEST_ASSERT_FALSE(obd_client_poll(&client, t + OBD_CLIENT_REPLY_TIMEOUT_US - 1U, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(0U, client.timeouts);
    t += OBD_CLIENT_REPLY_TIMEOUT_US; /* no reply */
    TEST_ASSERT_FALSE(obd_client_poll(&client, t, &id, frame));
    TEST_ASSERT_EQUAL_UINT32(1U, client.timeouts);
    t += OBD_CLIENT_GAP_US;
    expect_request(DTC_REQUEST, t);
    t += OBD_CLIENT_REPLY_TIMEOUT_US;
    (void)obd_client_poll(&client, t, &id, frame);
    t += OBD_CLIENT_GAP_US;
    expect_request(STATUS_REQUEST, t);
    for (size_t i = 0U; i <= OBD2_PID_COUNT; i++) {
        t += OBD_CLIENT_REPLY_TIMEOUT_US;
        (void)obd_client_poll(&client, t, &id, frame);
        t += OBD_CLIENT_GAP_US;
        const uint8_t pid = obd2_pid_at(i % OBD2_PID_COUNT)->pid; /* wraps to the first */
        const uint8_t expected[] = {0x02, 0x01, pid, CC, CC, CC, CC, CC};
        expect_request(expected, t);
    }
    TEST_ASSERT_EQUAL_UINT32(3U + OBD2_PID_COUNT + 1U, client.requests);
}

static void test_refreshes_codes_and_retries_the_vin(void)
{
    obd_client_start(&client, 0U);
    uint32_t t = 0U;
    expect_request(VIN_REQUEST, t);
    t = OBD_CLIENT_VIN_RETRY_US; /* the first VIN request failed long ago */
    TEST_ASSERT_FALSE(obd_client_poll(&client, t, &id, frame)); /* times out */
    t += OBD_CLIENT_GAP_US;
    expect_request(VIN_REQUEST, t); /* retried before anything else */

    /* With the VIN known, codes and status come round every refresh. */
    const uint8_t ff[] = {0x10, 0x14, 0x49, 0x02, 0x01, '1', 'M', '8'};
    const uint8_t cf1[] = {0x21, 'G', 'D', 'M', '9', 'A', 'X', 'K'};
    const uint8_t cf2[] = {0x22, 'P', '0', '4', '2', '7', '8', '8'};
    reply(ff, t);
    const uint8_t fc[] = {0x30, 0x00, 0x00, CC, CC, CC, CC, CC};
    expect_frame(OBD_CLIENT_FC_ID, fc, t);
    reply(cf1, t);
    reply(cf2, t);
    TEST_ASSERT_TRUE(client.vin_valid);
    t += OBD_CLIENT_GAP_US;
    expect_request(DTC_REQUEST, t); /* due since the start */
    const uint8_t dtcs[] = {0x06, 0x43, 0x02, 0x03, 0x01, 0x04, 0x20, CC};
    reply(dtcs, t);
    t += OBD_CLIENT_GAP_US;
    expect_request(STATUS_REQUEST, t);
    reply((const uint8_t[]){0x06, 0x41, 0x01, 0x82, 0x00, 0x00, 0x00, CC}, t);
    t += OBD_CLIENT_GAP_US;
    expect_request(RPM_REQUEST, t);
    reply(RPM_REPLY, t);
    t += OBD_CLIENT_REFRESH_US;
    expect_request(DTC_REQUEST, t);
    TEST_ASSERT_EQUAL_UINT32(4U, client.replies);
    TEST_ASSERT_EQUAL_UINT32(1U, client.timeouts);
}

/* ---- replies ---------------------------------------------------------------- */

static void test_keeps_the_values_it_is_sent(void)
{
    obd_client_start(&client, 0U);
    obd_client_frame(&client, 0x7E9U, RPM_REPLY, ISOTP_FRAME_LEN, 0U); /* another ECU's */
    TEST_ASSERT_FALSE(client.value_valid[0]);
    reply(RPM_REPLY, 0U); /* arrives with no request waiting: kept all the same */
    TEST_ASSERT_TRUE(client.value_valid[0]);
    TEST_ASSERT_EQUAL_INT32(1726, client.values[0]);
    TEST_ASSERT_EQUAL_UINT32(1U, client.replies);
    expect_request(VIN_REQUEST, 0U); /* the plan is unchanged */

    const uint8_t short_rpm[] = {0x03, 0x41, 0x0C, 0x1A, CC, CC, CC, CC};
    reply(short_rpm, 100U);
    TEST_ASSERT_FALSE(client.value_valid[0]); /* one byte short: forgotten */
    TEST_ASSERT_EQUAL_UINT32(1U, client.errors);

    const uint8_t bad_status[] = {0x03, 0x41, 0x01, 0x82, CC, CC, CC, CC};
    reply(bad_status, 100U);
    TEST_ASSERT_FALSE(client.status_valid);
    const uint8_t bad_dtcs[] = {0x03, 0x43, 0x02, 0x03, CC, CC, CC, CC};
    reply(bad_dtcs, 100U);
    TEST_ASSERT_FALSE(client.dtcs_valid);
    TEST_ASSERT_EQUAL_UINT8(0U, client.dtc_count);
    const uint8_t bad_vin[] = {0x03, 0x49, 0x02, 0x01, CC, CC, CC, CC};
    reply(bad_vin, 100U);
    TEST_ASSERT_FALSE(client.vin_valid);
    TEST_ASSERT_EQUAL_UINT32(4U, client.errors);
}

static void test_counts_replies_it_cannot_use(void)
{
    obd_client_start(&client, 0U);
    expect_request(VIN_REQUEST, 0U);
    const uint8_t negative[] = {0x03, 0x7F, 0x09, 0x12, CC, CC, CC, CC};
    reply(negative, 100U); /* "sub-function not supported" */
    TEST_ASSERT_EQUAL_UINT32(1U, client.errors);
    TEST_ASSERT_EQUAL_INT(OBD_CLIENT_GAP, client.state); /* the request is over */

    const uint8_t one_byte[] = {0x01, 0x41, CC, CC, CC, CC, CC, CC};
    const uint8_t unknown_pid[] = {0x03, 0x41, 0x0B, 0x64, CC, CC, CC, CC};
    reply(one_byte, 200U);
    reply(unknown_pid, 200U);
    TEST_ASSERT_EQUAL_UINT32(3U, client.errors);
    TEST_ASSERT_EQUAL_UINT32(0U, client.replies);
}

static void test_waits_for_a_reply_that_has_started(void)
{
    obd_client_start(&client, 0U);
    expect_request(VIN_REQUEST, 0U);
    const uint8_t ff[] = {0x10, 0x14, 0x49, 0x02, 0x01, '1', 'M', '8'};
    reply(ff, 40000U);
    const uint8_t fc[] = {0x30, 0x00, 0x00, CC, CC, CC, CC, CC};
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
    TEST_ASSERT_EQUAL_INT(OBD_CLIENT_GAP, client.state);
}

static void test_a_broken_reply_ends_the_request(void)
{
    obd_client_start(&client, 0U);
    expect_request(VIN_REQUEST, 0U);
    const uint8_t ff[] = {0x10, 0x14, 0x49, 0x02, 0x01, '1', 'M', '8'};
    reply(ff, 100U);
    TEST_ASSERT_TRUE(obd_client_poll(&client, 100U, &id, frame));
    obd_client_sent(&client, 100U);
    const uint8_t cf2[] = {0x22, 'P', '0', '4', '2', '7', '8', '8'};
    reply(cf2, 200U); /* CF 1 missing */
    TEST_ASSERT_EQUAL_UINT32(1U, client.errors);
    TEST_ASSERT_EQUAL_INT(OBD_CLIENT_GAP, client.state);
}

static void test_does_nothing_until_started_or_after_stopping(void)
{
    TEST_ASSERT_FALSE(obd_client_running(&client));
    TEST_ASSERT_FALSE(obd_client_poll(&client, 0U, &id, frame));
    reply(RPM_REPLY, 0U);
    TEST_ASSERT_FALSE(client.value_valid[0]);

    obd_client_start(&client, 0U);
    reply(RPM_REPLY, 0U);
    expect_request(VIN_REQUEST, 0U);
    obd_client_stop(&client);
    TEST_ASSERT_FALSE(obd_client_running(&client));
    TEST_ASSERT_FALSE(obd_client_poll(&client, 100000U, &id, frame));
    TEST_ASSERT_TRUE(client.value_valid[0]); /* answers so far are kept */
    TEST_ASSERT_EQUAL_UINT32(0U, client.timeouts);

    obd_client_start(&client, 200000U); /* a fresh start forgets them */
    TEST_ASSERT_FALSE(client.value_valid[0]);
    TEST_ASSERT_EQUAL_UINT32(0U, client.requests);
}

static void test_sent_with_nothing_pending_does_nothing(void)
{
    obd_client_start(&client, 0U);
    obd_client_sent(&client, 0U);
    TEST_ASSERT_EQUAL_UINT32(0U, client.requests);
    TEST_ASSERT_EQUAL_INT(OBD_CLIENT_GAP, client.state);
    TEST_ASSERT_FALSE(obd_client_poll(&client, 0U - 1U, &id, frame)); /* the gap isn't over */
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_reads_everything_from_the_simulated_ecu);
    UNIT_TEST(test_asks_vin_codes_status_then_each_pid);
    UNIT_TEST(test_refreshes_codes_and_retries_the_vin);
    UNIT_TEST(test_keeps_the_values_it_is_sent);
    UNIT_TEST(test_counts_replies_it_cannot_use);
    UNIT_TEST(test_waits_for_a_reply_that_has_started);
    UNIT_TEST(test_a_broken_reply_ends_the_request);
    UNIT_TEST(test_does_nothing_until_started_or_after_stopping);
    UNIT_TEST(test_sent_with_nothing_pending_does_nothing);
    return UNITY_END();
}
