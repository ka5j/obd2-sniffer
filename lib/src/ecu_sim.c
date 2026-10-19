#include "ecu_sim.h"

#include <stdbool.h>

#include "obd2.h"

#define IDLE_LOAD       180 /* tenths of a percent */
#define COOLANT_START_C 20
#define COOLANT_WARM_C  90
#define WARM_UP_S       90U
#define INTAKE_C        25
#define RUN_TIME_MAX_S  65535U

#define MASK_BYTES     4U    /* a supported-PIDs reply: a 32-bit mask */
#define MIL_ON         0x80U /* monitor status byte A: the warning light */
#define INFO_VIN_BIT   0x40U /* service 09 mask, byte A: PID 02 */
#define VIN_DATA_ITEMS 1U

typedef struct {
    uint32_t at_ms;
    int32_t rpm;
    int32_t speed_kmh;
    int32_t throttle; /* tenths of a percent */
} keyframe_t;

static int32_t between(int32_t from, int32_t to, uint32_t pos, uint32_t span)
{
    return from + (((to - from) * (int32_t)pos) / (int32_t)span);
}

void ecu_sim_state(uint32_t elapsed_ms, ecu_sim_state_t *state)
{
    /* One cycle of the drive. Values between two keyframes change linearly. */
    static const keyframe_t drive[] = {
        {0U, 800, 0, 0},               /* idle */
        {5000U, 800, 0, 0},            /* start accelerating */
        {9000U, 3200, 50, 450},        /* ease off */
        {12000U, 2400, 70, 220},       /* settle into a cruise */
        {20000U, 2200, 80, 180},       /* slow down */
        {25000U, 900, 15, 0},          /* nearly stopped */
        {27000U, 800, 0, 0},           /* idle */
        {ECU_SIM_CYCLE_MS, 800, 0, 0}, /* idle, as the next cycle starts */
    };
    uint32_t t = elapsed_ms % ECU_SIM_CYCLE_MS;
    size_t next = 1U;
    while (drive[next].at_ms <= t) { /* the last keyframe is after any t */
        next++;
    }
    const keyframe_t *a = &drive[next - 1U];
    const keyframe_t *b = &drive[next];
    uint32_t pos = t - a->at_ms;
    uint32_t span = b->at_ms - a->at_ms;
    state->rpm = between(a->rpm, b->rpm, pos, span);
    state->speed_kmh = between(a->speed_kmh, b->speed_kmh, pos, span);
    state->throttle = between(a->throttle, b->throttle, pos, span);
    state->load = IDLE_LOAD + ((state->throttle * 3) / 2);

    uint32_t seconds = elapsed_ms / 1000U;
    if (seconds >= WARM_UP_S) {
        state->coolant_c = COOLANT_WARM_C;
    } else {
        int32_t warming =
            ((int32_t)seconds * (COOLANT_WARM_C - COOLANT_START_C)) / (int32_t)WARM_UP_S;
        state->coolant_c = COOLANT_START_C + warming;
    }
    state->intake_c = INTAKE_C;
    state->run_time_s = (int32_t)((seconds > RUN_TIME_MAX_S) ? RUN_TIME_MAX_S : seconds);
}

/* The value the engine reports for a PID from obd2.h's table. */
static int32_t value_of(const ecu_sim_state_t *s, uint8_t pid)
{
    int32_t value;
    switch (pid) {
    case OBD2_PID_ENGINE_SPEED:
        value = s->rpm;
        break;
    case OBD2_PID_VEHICLE_SPEED:
        value = s->speed_kmh;
        break;
    case OBD2_PID_THROTTLE:
        value = s->throttle;
        break;
    case OBD2_PID_ENGINE_LOAD:
        value = s->load;
        break;
    case OBD2_PID_COOLANT_TEMP:
        value = s->coolant_c;
        break;
    case OBD2_PID_INTAKE_TEMP:
        value = s->intake_c;
        break;
    default: /* OBD2_PID_RUN_TIME, the last one in the table */
        value = s->run_time_s;
        break;
    }
    return value;
}

/* A PID's bit in a supported-PIDs mask: PID 01 is the top bit, PID 20 the
 * bottom one. The masking keeps the shift in range for any input. */
static uint32_t pid_bit(uint8_t pid)
{
    return 0x80000000U >> ((pid - 1U) & 0x1FU);
}

static uint32_t supported_mask(void)
{
    uint32_t mask = pid_bit(OBD2_PID_MONITOR_STATUS);
    for (size_t i = 0U; i < obd2_pid_count(); i++) {
        mask |= pid_bit(obd2_pid_at(i)->pid);
    }
    return mask;
}

static size_t current_data(uint32_t elapsed_ms, uint8_t pid, uint8_t *out)
{
    out[0] = OBD2_SERVICE_CURRENT_DATA + OBD2_REPLY_OFFSET;
    out[1] = pid;
    if (pid == OBD2_PID_SUPPORTED_01_20) {
        uint32_t mask = supported_mask();
        for (uint32_t i = 0U; i < MASK_BYTES; i++) {
            out[2U + i] = (uint8_t)(mask >> (24U - (8U * i)));
        }
        return 2U + MASK_BYTES;
    }
    if (pid == OBD2_PID_MONITOR_STATUS) {
        out[2] = MIL_ON | ECU_SIM_DTC_COUNT;
        out[3] = 0U; /* readiness of the emissions self-tests: not simulated */
        out[4] = 0U;
        out[5] = 0U;
        return 6U;
    }
    const obd2_pid_t *info = obd2_pid_find(pid);
    if (info == NULL) {
        return 0U;
    }
    ecu_sim_state_t state;
    ecu_sim_state(elapsed_ms, &state);
    return 2U + obd2_encode(info, value_of(&state, pid), &out[2], ECU_SIM_REPLY_MAX - 2U);
}

static size_t stored_dtcs(uint8_t *out)
{
    static const uint16_t codes[ECU_SIM_DTC_COUNT] = {
        0x0301U, /* P0301: cylinder 1 misfire */
        0x0420U, /* P0420: catalytic converter efficiency below threshold */
    };
    out[0] = OBD2_SERVICE_STORED_DTCS + OBD2_REPLY_OFFSET;
    out[1] = ECU_SIM_DTC_COUNT;
    for (size_t i = 0U; i < ECU_SIM_DTC_COUNT; i++) {
        out[2U + (2U * i)] = (uint8_t)(codes[i] >> 8U);
        out[3U + (2U * i)] = (uint8_t)(codes[i] & 0xFFU);
    }
    return 2U + (2U * ECU_SIM_DTC_COUNT);
}

static size_t vehicle_info(uint8_t pid, uint8_t *out)
{
    out[0] = OBD2_SERVICE_VEHICLE_INFO + OBD2_REPLY_OFFSET;
    out[1] = pid;
    if (pid == OBD2_INFO_SUPPORTED) {
        out[2] = INFO_VIN_BIT;
        out[3] = 0U;
        out[4] = 0U;
        out[5] = 0U;
        return 2U + MASK_BYTES;
    }
    if (pid == OBD2_INFO_VIN) {
        static const char vin[] = ECU_SIM_VIN;
        out[2] = VIN_DATA_ITEMS;
        for (size_t i = 0U; i < OBD2_VIN_LEN; i++) {
            out[3U + i] = (uint8_t)vin[i];
        }
        return 3U + OBD2_VIN_LEN;
    }
    return 0U;
}

size_t ecu_sim_reply(uint32_t elapsed_ms, const uint8_t *request, size_t len, uint8_t *out,
                     size_t size)
{
    if ((len == 0U) || (size < ECU_SIM_REPLY_MAX)) {
        return 0U;
    }
    size_t n = 0U;
    switch (request[0]) {
    case OBD2_SERVICE_CURRENT_DATA:
        if (len == 2U) {
            n = current_data(elapsed_ms, request[1], out);
        }
        break;
    case OBD2_SERVICE_STORED_DTCS:
        if (len == 1U) {
            n = stored_dtcs(out);
        }
        break;
    case OBD2_SERVICE_VEHICLE_INFO:
        if (len == 2U) {
            n = vehicle_info(request[1], out);
        }
        break;
    default:
        break;
    }
    return n;
}
