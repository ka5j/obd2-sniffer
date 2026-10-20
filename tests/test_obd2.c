#include <stdint.h>
#include <string.h>

#include "obd2.h"
#include "test_support.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static int32_t decode(uint8_t pid, const uint8_t *data, size_t len)
{
    int32_t value = 0x7FFFFFFF;
    TEST_ASSERT_TRUE(obd2_decode(obd2_pid_find(pid), data, len, &value));
    return value;
}

static void test_pid_table(void)
{
    TEST_ASSERT_EQUAL_size_t(7U, obd2_pid_count());
    const obd2_pid_t *first = obd2_pid_at(0U);
    TEST_ASSERT_EQUAL_HEX8(OBD2_PID_ENGINE_SPEED, first->pid);
    TEST_ASSERT_EQUAL_STRING("obd_rpm", first->key);
    TEST_ASSERT_EQUAL_STRING("Engine (rpm)", first->label);
    TEST_ASSERT_NULL(obd2_pid_at(obd2_pid_count()));

    TEST_ASSERT_EQUAL_PTR(first, obd2_pid_find(OBD2_PID_ENGINE_SPEED));
    TEST_ASSERT_EQUAL_UINT8(1U, obd2_pid_find(OBD2_PID_THROTTLE)->decimals);
    TEST_ASSERT_NULL(obd2_pid_find(OBD2_PID_MONITOR_STATUS)); /* not a single number */
    TEST_ASSERT_NULL(obd2_pid_find(0xA6U));

    TEST_ASSERT_EQUAL_UINT8(2U, obd2_pid_bytes(obd2_pid_find(OBD2_PID_ENGINE_SPEED)));
    TEST_ASSERT_EQUAL_UINT8(2U, obd2_pid_bytes(obd2_pid_find(OBD2_PID_RUN_TIME)));
    TEST_ASSERT_EQUAL_UINT8(1U, obd2_pid_bytes(obd2_pid_find(OBD2_PID_VEHICLE_SPEED)));
}

/* Worked examples, from replies a real ECU would send. */
static void test_decode_examples(void)
{
    const uint8_t rpm[] = {0x1A, 0xF8};
    TEST_ASSERT_EQUAL_INT32(1726, decode(OBD2_PID_ENGINE_SPEED, rpm, 2U)); /* 6904 / 4 */
    const uint8_t speed[] = {0x32};
    TEST_ASSERT_EQUAL_INT32(50, decode(OBD2_PID_VEHICLE_SPEED, speed, 1U));
    const uint8_t throttle[] = {0x3C};
    TEST_ASSERT_EQUAL_INT32(235, decode(OBD2_PID_THROTTLE, throttle, 1U)); /* 23.5% */
    const uint8_t full[] = {0xFF};
    TEST_ASSERT_EQUAL_INT32(1000, decode(OBD2_PID_ENGINE_LOAD, full, 1U));
    const uint8_t coolant[] = {0x7B};
    TEST_ASSERT_EQUAL_INT32(83, decode(OBD2_PID_COOLANT_TEMP, coolant, 1U));
    const uint8_t freezing[] = {0x00};
    TEST_ASSERT_EQUAL_INT32(-40, decode(OBD2_PID_INTAKE_TEMP, freezing, 1U));
    const uint8_t run_time[] = {0x02, 0xF2};
    TEST_ASSERT_EQUAL_INT32(754, decode(OBD2_PID_RUN_TIME, run_time, 2U));
}

static void test_decode_needs_enough_bytes(void)
{
    const uint8_t data[] = {0x1A, 0xF8};
    int32_t value = 0;
    TEST_ASSERT_FALSE(obd2_decode(obd2_pid_find(OBD2_PID_ENGINE_SPEED), data, 1U, &value));
    TEST_ASSERT_FALSE(obd2_decode(obd2_pid_find(OBD2_PID_VEHICLE_SPEED), data, 0U, &value));
}

static void test_encode_examples_and_limits(void)
{
    uint8_t out[2] = {0};
    TEST_ASSERT_EQUAL_size_t(2U, obd2_encode(obd2_pid_find(OBD2_PID_ENGINE_SPEED), 1726, out, 2U));
    TEST_ASSERT_EQUAL_HEX8(0x1A, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, out[1]);

    const struct {
        uint8_t pid;
        int32_t value;
        uint8_t a;
    } cases[] = {
        {OBD2_PID_THROTTLE, 235, 0x3C},     {OBD2_PID_THROTTLE, -5, 0x00},
        {OBD2_PID_THROTTLE, 1200, 0xFF},    {OBD2_PID_COOLANT_TEMP, 83, 0x7B},
        {OBD2_PID_COOLANT_TEMP, -50, 0x00}, {OBD2_PID_COOLANT_TEMP, 300, 0xFF},
        {OBD2_PID_VEHICLE_SPEED, 50, 0x32}, {OBD2_PID_VEHICLE_SPEED, 400, 0xFF},
    };
    for (size_t i = 0U; i < (sizeof cases / sizeof cases[0]); i++) {
        TEST_ASSERT_EQUAL_size_t(
            1U, obd2_encode(obd2_pid_find(cases[i].pid), cases[i].value, out, sizeof out));
        TEST_ASSERT_EQUAL_HEX8(cases[i].a, out[0]);
    }

    (void)obd2_encode(obd2_pid_find(OBD2_PID_ENGINE_SPEED), 20000, out, 2U); /* above 16383 */
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, out[1]);
    (void)obd2_encode(obd2_pid_find(OBD2_PID_RUN_TIME), 70000, out, 2U);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]);

    TEST_ASSERT_EQUAL_size_t(0U, obd2_encode(obd2_pid_find(OBD2_PID_ENGINE_SPEED), 1, out, 1U));
}

/* Every value a PID can express survives encode then decode: exactly for
 * whole numbers, within one 1/255 step for percentages. */
static void test_encode_then_decode(void)
{
    const struct {
        uint8_t pid;
        int32_t low;
        int32_t high;
        int32_t tolerance;
    } ranges[] = {
        {OBD2_PID_ENGINE_SPEED, 0, 16383, 0}, {OBD2_PID_VEHICLE_SPEED, 0, 255, 0},
        {OBD2_PID_THROTTLE, 0, 1000, 2},      {OBD2_PID_ENGINE_LOAD, 0, 1000, 2},
        {OBD2_PID_COOLANT_TEMP, -40, 215, 0}, {OBD2_PID_INTAKE_TEMP, -40, 215, 0},
        {OBD2_PID_RUN_TIME, 0, 65535, 0},
    };
    TEST_ASSERT_EQUAL_size_t(obd2_pid_count(), sizeof ranges / sizeof ranges[0]);
    uint8_t out[2];
    for (size_t i = 0U; i < (sizeof ranges / sizeof ranges[0]); i++) {
        const obd2_pid_t *info = obd2_pid_find(ranges[i].pid);
        for (int32_t v = ranges[i].low; v <= ranges[i].high; v++) {
            int32_t back = 0;
            size_t n = obd2_encode(info, v, out, sizeof out);
            TEST_ASSERT_TRUE(obd2_decode(info, out, n, &back));
            TEST_ASSERT_INT32_WITHIN(ranges[i].tolerance, v, back);
        }
    }
}

static void test_monitor_status(void)
{
    const uint8_t two_codes_mil_on[] = {0x82, 0x07, 0xE5, 0x00};
    bool mil = false;
    uint8_t count = 0U;
    TEST_ASSERT_TRUE(obd2_decode_monitor_status(two_codes_mil_on, 4U, &mil, &count));
    TEST_ASSERT_TRUE(mil);
    TEST_ASSERT_EQUAL_UINT8(2U, count);

    const uint8_t clean[] = {0x00, 0x07, 0xE5, 0x00};
    TEST_ASSERT_TRUE(obd2_decode_monitor_status(clean, 4U, &mil, &count));
    TEST_ASSERT_FALSE(mil);
    TEST_ASSERT_EQUAL_UINT8(0U, count);

    TEST_ASSERT_FALSE(obd2_decode_monitor_status(clean, 3U, &mil, &count));
}

static void test_parse_dtcs(void)
{
    uint16_t codes[4] = {0};
    const uint8_t two[] = {0x43, 0x02, 0x03, 0x01, 0x04, 0x20};
    TEST_ASSERT_EQUAL_INT32(2, obd2_parse_dtcs(two, sizeof two, codes, 4U));
    TEST_ASSERT_EQUAL_HEX16(0x0301U, codes[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0420U, codes[1]);
    TEST_ASSERT_EQUAL_INT32(1, obd2_parse_dtcs(two, sizeof two, codes, 1U)); /* room for one */

    const uint8_t none[] = {0x43, 0x00};
    TEST_ASSERT_EQUAL_INT32(0, obd2_parse_dtcs(none, sizeof none, codes, 4U));

    const uint8_t wrong_service[] = {0x47, 0x00};
    TEST_ASSERT_EQUAL_INT32(-1, obd2_parse_dtcs(wrong_service, sizeof wrong_service, codes, 4U));
    TEST_ASSERT_EQUAL_INT32(-1, obd2_parse_dtcs(two, 1U, codes, 4U));
    TEST_ASSERT_EQUAL_INT32(-1, obd2_parse_dtcs(two, 5U, codes, 4U)); /* second code cut off */
}

static void test_dtc_text(void)
{
    const struct {
        uint16_t code;
        const char *text;
    } cases[] = {
        {0x0301U, "P0301"}, {0x2ABCU, "P2ABC"}, {0x4123U, "C0123"},
        {0x9234U, "B1234"}, {0xC100U, "U0100"}, {0xFFFFU, "U3FFF"},
    };
    char text[OBD2_DTC_TEXT_LEN];
    for (size_t i = 0U; i < (sizeof cases / sizeof cases[0]); i++) {
        obd2_dtc_text(cases[i].code, text);
        TEST_ASSERT_EQUAL_STRING(cases[i].text, text);
    }
}

static void test_parse_vin(void)
{
    uint8_t reply[] = {0x49, 0x02, 0x01, '1', 'M', '8', 'G', 'D', 'M', '9',
                       'A',  'X',  'K',  'P', '0', '4', '2', '7', '8', '8'};
    char vin[OBD2_VIN_LEN + 1U];
    TEST_ASSERT_TRUE(obd2_parse_vin(reply, sizeof reply, vin));
    TEST_ASSERT_EQUAL_STRING("1M8GDM9AXKP042788", vin);

    TEST_ASSERT_FALSE(obd2_parse_vin(reply, sizeof reply - 1U, vin)); /* too short */
    reply[0] = 0x41;
    TEST_ASSERT_FALSE(obd2_parse_vin(reply, sizeof reply, vin)); /* another service */
    reply[0] = 0x49;
    reply[1] = 0x04;
    TEST_ASSERT_FALSE(obd2_parse_vin(reply, sizeof reply, vin)); /* another PID */
    reply[1] = 0x02;
    reply[2] = 0x02;
    TEST_ASSERT_FALSE(obd2_parse_vin(reply, sizeof reply, vin)); /* two data items */
    reply[2] = 0x01;
    reply[10] = ' ';
    TEST_ASSERT_FALSE(obd2_parse_vin(reply, sizeof reply, vin)); /* not printable */
    reply[10] = 0x7F;
    TEST_ASSERT_FALSE(obd2_parse_vin(reply, sizeof reply, vin));
    reply[10] = 'A';
    TEST_ASSERT_TRUE(obd2_parse_vin(reply, sizeof reply, vin));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_pid_table);
    UNIT_TEST(test_decode_examples);
    UNIT_TEST(test_decode_needs_enough_bytes);
    UNIT_TEST(test_encode_examples_and_limits);
    UNIT_TEST(test_encode_then_decode);
    UNIT_TEST(test_monitor_status);
    UNIT_TEST(test_parse_dtcs);
    UNIT_TEST(test_dtc_text);
    UNIT_TEST(test_parse_vin);
    return UNITY_END();
}
