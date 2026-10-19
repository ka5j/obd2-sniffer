#include <stdint.h>
#include <string.h>

#include "ecu_sim.h"
#include "obd2.h"
#include "test_support.h"

static uint8_t out[ECU_SIM_REPLY_MAX];

void setUp(void)
{
    (void)memset(out, 0, sizeof out);
}

void tearDown(void)
{
}

static size_t reply(uint32_t elapsed_ms, const uint8_t *request, size_t len)
{
    return ecu_sim_reply(elapsed_ms, request, len, out, sizeof out);
}

static void check_state(uint32_t elapsed_ms, int32_t rpm, int32_t speed, int32_t throttle,
                        int32_t coolant)
{
    ecu_sim_state_t s;
    ecu_sim_state(elapsed_ms, &s);
    TEST_ASSERT_EQUAL_INT32(rpm, s.rpm);
    TEST_ASSERT_EQUAL_INT32(speed, s.speed_kmh);
    TEST_ASSERT_EQUAL_INT32(throttle, s.throttle);
    TEST_ASSERT_EQUAL_INT32(180 + ((throttle * 3) / 2), s.load);
    TEST_ASSERT_EQUAL_INT32(coolant, s.coolant_c);
    TEST_ASSERT_EQUAL_INT32(25, s.intake_c);
}

static void test_drive_cycle(void)
{
    check_state(0U, 800, 0, 0, 20);        /* idle, cold */
    check_state(7000U, 2000, 25, 225, 25); /* halfway through accelerating */
    check_state(9000U, 3200, 50, 450, 27); /* on a keyframe */
    check_state(29999U, 800, 0, 0, 42);    /* end of the cycle */
    /* 15 s into the second cycle: 3/8 of the way from 2400 to 2200 rpm. */
    check_state(45000U, 2325, 73, 205, 55);
}

static void test_coolant_warms_up_then_holds(void)
{
    ecu_sim_state_t s;
    ecu_sim_state(89999U, &s);
    TEST_ASSERT_EQUAL_INT32(89, s.coolant_c);
    ecu_sim_state(90000U, &s);
    TEST_ASSERT_EQUAL_INT32(90, s.coolant_c);
    ecu_sim_state(3600000U, &s);
    TEST_ASSERT_EQUAL_INT32(90, s.coolant_c);
}

static void test_run_time_counts_seconds_up_to_its_limit(void)
{
    ecu_sim_state_t s;
    ecu_sim_state(754999U, &s);
    TEST_ASSERT_EQUAL_INT32(754, s.run_time_s);
    ecu_sim_state(0xFFFFFFFFU, &s); /* 49 days */
    TEST_ASSERT_EQUAL_INT32(65535, s.run_time_s);
}

static void test_supported_pids(void)
{
    const uint8_t request[] = {0x01, 0x00};
    const uint8_t expected[] = {0x41, 0x00, 0x98, 0x1A, 0x80, 0x02}; /* 01 04 05 0C 0D 0F 11 1F */
    TEST_ASSERT_EQUAL_size_t(sizeof expected, reply(0U, request, sizeof request));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

static void test_monitor_status_reports_two_codes_and_the_warning_light(void)
{
    const uint8_t request[] = {0x01, 0x01};
    const uint8_t expected[] = {0x41, 0x01, 0x82, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof expected, reply(0U, request, sizeof request));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

static void test_engine_speed_reply(void)
{
    const uint8_t request[] = {0x01, 0x0C};
    const uint8_t expected[] = {0x41, 0x0C, 0x0C, 0x80}; /* 3200 / 4 = 800 rpm */
    TEST_ASSERT_EQUAL_size_t(sizeof expected, reply(0U, request, sizeof request));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

/* Each PID in the table is answered with the engine's own value, so a PID
 * added to the table without a value here would be caught. */
static void test_every_pid_reports_the_engine_state(void)
{
    const uint32_t t = 45000U;
    ecu_sim_state_t s;
    ecu_sim_state(t, &s);
    const struct {
        uint8_t pid;
        int32_t value;
    } expected[] = {
        {OBD2_PID_ENGINE_SPEED, s.rpm},       {OBD2_PID_VEHICLE_SPEED, s.speed_kmh},
        {OBD2_PID_THROTTLE, s.throttle},      {OBD2_PID_ENGINE_LOAD, s.load},
        {OBD2_PID_COOLANT_TEMP, s.coolant_c}, {OBD2_PID_INTAKE_TEMP, s.intake_c},
        {OBD2_PID_RUN_TIME, s.run_time_s},
    };
    TEST_ASSERT_EQUAL_size_t(obd2_pid_count(), sizeof expected / sizeof expected[0]);
    for (size_t i = 0U; i < (sizeof expected / sizeof expected[0]); i++) {
        const obd2_pid_t *info = obd2_pid_find(expected[i].pid);
        const uint8_t request[] = {0x01, expected[i].pid};
        size_t n = reply(t, request, sizeof request);
        TEST_ASSERT_EQUAL_size_t(2U + obd2_pid_bytes(info), n);
        TEST_ASSERT_EQUAL_HEX8(0x41, out[0]);
        TEST_ASSERT_EQUAL_HEX8(expected[i].pid, out[1]);
        int32_t value = 0;
        TEST_ASSERT_TRUE(obd2_decode(info, &out[2], n - 2U, &value));
        TEST_ASSERT_INT32_WITHIN(2, expected[i].value, value); /* percentages: 1/255 steps */
    }
}

static void test_stored_trouble_codes(void)
{
    const uint8_t request[] = {0x03};
    const uint8_t expected[] = {0x43, 0x02, 0x03, 0x01, 0x04, 0x20}; /* P0301, P0420 */
    TEST_ASSERT_EQUAL_size_t(sizeof expected, reply(0U, request, sizeof request));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

static void test_vehicle_information(void)
{
    const uint8_t supported[] = {0x09, 0x00};
    const uint8_t expected_supported[] = {0x49, 0x00, 0x40, 0x00, 0x00, 0x00}; /* PID 02 */
    TEST_ASSERT_EQUAL_size_t(6U, reply(0U, supported, sizeof supported));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_supported, out, 6U);

    const uint8_t vin[] = {0x09, 0x02};
    TEST_ASSERT_EQUAL_size_t(20U, reply(0U, vin, sizeof vin));
    char text[OBD2_VIN_LEN + 1U];
    TEST_ASSERT_TRUE(obd2_parse_vin(out, 20U, text));
    TEST_ASSERT_EQUAL_STRING(ECU_SIM_VIN, text);
}

static void test_silent_for_anything_unsupported(void)
{
    const uint8_t unknown_pid[] = {0x01, 0x02};
    const uint8_t two_pids[] = {0x01, 0x0C, 0x0D};
    const uint8_t no_pid[] = {0x01};
    const uint8_t dtcs_with_pid[] = {0x03, 0x00};
    const uint8_t calibration_id[] = {0x09, 0x04};
    const uint8_t info_no_pid[] = {0x09};
    const uint8_t clear_dtcs[] = {0x04}; /* clearing codes is not simulated */
    TEST_ASSERT_EQUAL_size_t(0U, reply(0U, unknown_pid, sizeof unknown_pid));
    TEST_ASSERT_EQUAL_size_t(0U, reply(0U, two_pids, sizeof two_pids));
    TEST_ASSERT_EQUAL_size_t(0U, reply(0U, no_pid, sizeof no_pid));
    TEST_ASSERT_EQUAL_size_t(0U, reply(0U, dtcs_with_pid, sizeof dtcs_with_pid));
    TEST_ASSERT_EQUAL_size_t(0U, reply(0U, calibration_id, sizeof calibration_id));
    TEST_ASSERT_EQUAL_size_t(0U, reply(0U, info_no_pid, sizeof info_no_pid));
    TEST_ASSERT_EQUAL_size_t(0U, reply(0U, clear_dtcs, sizeof clear_dtcs));
}

static void test_needs_a_request_and_room_for_any_reply(void)
{
    const uint8_t request[] = {0x03};
    TEST_ASSERT_EQUAL_size_t(0U, ecu_sim_reply(0U, request, 0U, out, sizeof out));
    TEST_ASSERT_EQUAL_size_t(0U, ecu_sim_reply(0U, request, 1U, out, ECU_SIM_REPLY_MAX - 1U));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_drive_cycle);
    UNIT_TEST(test_coolant_warms_up_then_holds);
    UNIT_TEST(test_run_time_counts_seconds_up_to_its_limit);
    UNIT_TEST(test_supported_pids);
    UNIT_TEST(test_monitor_status_reports_two_codes_and_the_warning_light);
    UNIT_TEST(test_engine_speed_reply);
    UNIT_TEST(test_every_pid_reports_the_engine_state);
    UNIT_TEST(test_stored_trouble_codes);
    UNIT_TEST(test_vehicle_information);
    UNIT_TEST(test_silent_for_anything_unsupported);
    UNIT_TEST(test_needs_a_request_and_room_for_any_reply);
    return UNITY_END();
}
