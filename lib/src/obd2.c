#include "obd2.h"

#define MONITOR_STATUS_BYTES 4U
#define MIL_BIT              0x80U
#define DTC_COUNT_MASK       0x7FU
#define DTC_BYTES            2U
#define VIN_REPLY_LEN        (3U + OBD2_VIN_LEN) /* 49 02 01, then the characters */
#define VIN_ITEMS            1U                  /* the VIN is one data item */
#define PRINTABLE_FIRST      0x21U               /* '!': VINs have no spaces */
#define PRINTABLE_LAST       0x7EU               /* '~' */

#define PERCENT_TENTHS_MAX 1000
#define TEMP_OFFSET        40
#define TEMP_MAX           215   /* 255 - 40 */
#define RPM_MAX            16383 /* 0xFFFF / 4 */
#define BYTE_MAX           255
#define WORD_MAX           65535

static const obd2_pid_t PIDS[] = {
    {OBD2_PID_ENGINE_SPEED, OBD2_FORMULA_RPM, 0U, "obd_rpm", "Engine speed", "rpm"},
    {OBD2_PID_VEHICLE_SPEED, OBD2_FORMULA_BYTE, 0U, "obd_speed", "Vehicle speed", "km/h"},
    {OBD2_PID_THROTTLE, OBD2_FORMULA_PERCENT, 1U, "obd_throttle", "Throttle", "%"},
    {OBD2_PID_ENGINE_LOAD, OBD2_FORMULA_PERCENT, 1U, "obd_load", "Engine load", "%"},
    {OBD2_PID_COOLANT_TEMP, OBD2_FORMULA_TEMPERATURE, 0U, "obd_coolant", "Coolant", "C"},
    {OBD2_PID_INTAKE_TEMP, OBD2_FORMULA_TEMPERATURE, 0U, "obd_intake", "Intake air", "C"},
    {OBD2_PID_RUN_TIME, OBD2_FORMULA_WORD, 0U, "obd_run_time", "Engine run time", "s"},
};

size_t obd2_pid_count(void)
{
    return sizeof(PIDS) / sizeof(PIDS[0]);
}

const obd2_pid_t *obd2_pid_at(size_t index)
{
    return (index < obd2_pid_count()) ? &PIDS[index] : NULL;
}

const obd2_pid_t *obd2_pid_find(uint8_t pid)
{
    for (size_t i = 0U; i < obd2_pid_count(); i++) {
        if (PIDS[i].pid == pid) {
            return &PIDS[i];
        }
    }
    return NULL;
}

uint8_t obd2_pid_bytes(const obd2_pid_t *info)
{
    bool two = (info->formula == OBD2_FORMULA_RPM) || (info->formula == OBD2_FORMULA_WORD);
    return two ? 2U : 1U;
}

bool obd2_decode(const obd2_pid_t *info, const uint8_t *data, size_t len, int32_t *value)
{
    if (len < obd2_pid_bytes(info)) {
        return false;
    }
    int32_t a = (int32_t)data[0];
    switch (info->formula) {
    case OBD2_FORMULA_PERCENT:
        *value = ((a * PERCENT_TENTHS_MAX) + 127) / 255; /* rounded */
        break;
    case OBD2_FORMULA_TEMPERATURE:
        *value = a - TEMP_OFFSET;
        break;
    case OBD2_FORMULA_RPM:
        *value = ((a * 256) + (int32_t)data[1]) / 4;
        break;
    case OBD2_FORMULA_WORD:
        *value = (a * 256) + (int32_t)data[1];
        break;
    default: /* OBD2_FORMULA_BYTE */
        *value = a;
        break;
    }
    return true;
}

static int32_t clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return (value > high) ? high : value;
}

size_t obd2_encode(const obd2_pid_t *info, int32_t value, uint8_t *out, size_t size)
{
    uint8_t bytes = obd2_pid_bytes(info);
    if (size < bytes) {
        return 0U;
    }
    int32_t raw;
    switch (info->formula) {
    case OBD2_FORMULA_PERCENT:
        raw = ((clamp(value, 0, PERCENT_TENTHS_MAX) * 255) + 500) / PERCENT_TENTHS_MAX;
        break;
    case OBD2_FORMULA_TEMPERATURE:
        raw = clamp(value, -TEMP_OFFSET, TEMP_MAX) + TEMP_OFFSET;
        break;
    case OBD2_FORMULA_RPM:
        raw = clamp(value, 0, RPM_MAX) * 4;
        break;
    case OBD2_FORMULA_WORD:
        raw = clamp(value, 0, WORD_MAX);
        break;
    default: /* OBD2_FORMULA_BYTE */
        raw = clamp(value, 0, BYTE_MAX);
        break;
    }
    uint32_t bits = (uint32_t)raw;
    if (bytes == 2U) {
        out[0] = (uint8_t)(bits >> 8U);
        out[1] = (uint8_t)(bits & 0xFFU);
    } else {
        out[0] = (uint8_t)bits;
    }
    return bytes;
}

bool obd2_decode_monitor_status(const uint8_t *data, size_t len, bool *mil_on, uint8_t *dtc_count)
{
    if (len < MONITOR_STATUS_BYTES) {
        return false;
    }
    *mil_on = (data[0] & MIL_BIT) != 0U;
    *dtc_count = (uint8_t)(data[0] & DTC_COUNT_MASK);
    return true;
}

int32_t obd2_parse_dtcs(const uint8_t *reply, size_t len, uint16_t *codes, size_t max)
{
    if ((len < 2U) || (reply[0] != (OBD2_SERVICE_STORED_DTCS + OBD2_REPLY_OFFSET))) {
        return -1;
    }
    size_t count = reply[1];
    if (len < (2U + (count * DTC_BYTES))) {
        return -1;
    }
    size_t kept = (count < max) ? count : max;
    for (size_t i = 0U; i < kept; i++) {
        size_t pos = 2U + (i * DTC_BYTES);
        codes[i] = (uint16_t)((uint16_t)reply[pos] << 8U) | reply[pos + 1U];
    }
    return (int32_t)kept;
}

void obd2_dtc_text(uint16_t code, char out[OBD2_DTC_TEXT_LEN])
{
    static const char systems[] = "PCBU";
    static const char digits[] = "0123456789ABCDEF";
    out[0] = systems[code >> 14U];
    out[1] = digits[(code >> 12U) & 0x3U];
    out[2] = digits[(code >> 8U) & 0xFU];
    out[3] = digits[(code >> 4U) & 0xFU];
    out[4] = digits[code & 0xFU];
    out[5] = '\0';
}

bool obd2_parse_vin(const uint8_t *reply, size_t len, char out[OBD2_VIN_LEN + 1U])
{
    if ((len != VIN_REPLY_LEN) || (reply[0] != (OBD2_SERVICE_VEHICLE_INFO + OBD2_REPLY_OFFSET)) ||
        (reply[1] != OBD2_INFO_VIN) || (reply[2] != VIN_ITEMS)) {
        return false;
    }
    for (size_t i = 3U; i < VIN_REPLY_LEN; i++) {
        if ((reply[i] < PRINTABLE_FIRST) || (reply[i] > PRINTABLE_LAST)) {
            return false;
        }
    }
    for (size_t i = 0U; i < OBD2_VIN_LEN; i++) {
        out[i] = (char)reply[3U + i];
    }
    out[OBD2_VIN_LEN] = '\0';
    return true;
}
