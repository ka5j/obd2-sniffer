/*
 * A simulated engine control unit (ECU) that answers OBD2 requests, so the
 * diagnostic side of the project can be built and shown without a car.
 *
 * Its engine repeats a 30-second drive: idle, accelerate, ease off to a
 * cruise, slow down, idle. The coolant warms from 20 to 90 C over the first
 * 90 seconds. It has two stored trouble codes, P0301 (cylinder 1 misfire)
 * and P0420 (catalytic converter below threshold), so the warning light is
 * on, and a fixed VIN with a valid check digit.
 *
 * Every value is a function of the time since the simulation started, with
 * no hidden state, so tests can check exact replies.
 */
#ifndef ECU_SIM_H
#define ECU_SIM_H

#include <stddef.h>
#include <stdint.h>

#define ECU_SIM_CYCLE_MS  30000U
#define ECU_SIM_VIN       "1M8GDM9AXKP042788"
#define ECU_SIM_DTC_COUNT 2U
#define ECU_SIM_REPLY_MAX 20U /* the VIN reply */

typedef struct {
    int32_t rpm;
    int32_t speed_kmh;
    int32_t throttle; /* tenths of a percent */
    int32_t load;     /* tenths of a percent */
    int32_t coolant_c;
    int32_t intake_c;
    int32_t run_time_s;
} ecu_sim_state_t;

/* The engine's state `elapsed_ms` after the simulation started. */
void ecu_sim_state(uint32_t elapsed_ms, ecu_sim_state_t *state);

/* The reply to one OBD2 request, as the ECU would send it. Returns its
 * length, or 0 for no reply: like a real ECU, the simulation stays silent
 * for services and PIDs it doesn't support. Supported:
 *   01 00, 01 01 and the PIDs in obd2.h's table (one PID per request)
 *   03
 *   09 00, 09 02 */
size_t ecu_sim_reply(uint32_t elapsed_ms, const uint8_t *request, size_t len, uint8_t *out,
                     size_t size);

#endif /* ECU_SIM_H */
