/*
 * OBD2 diagnostic messages (SAE J1979, also published as ISO 15031-5).
 *
 * Every car sold in the US since 1996 and in the EU since 2001 (petrol) or
 * 2004 (diesel) answers these on its diagnostic port. A request is a service
 * number and, for most services, a parameter ID (PID); the reply repeats
 * both, with 0x40 added to the service:
 *
 *   01 0C   current data: engine speed   ->  41 0C 1A F8         1726 rpm
 *   03      stored trouble codes         ->  43 02 03 01 04 20   P0301, P0420
 *   09 02   vehicle identification       ->  49 02 01 + the 17-character VIN
 *
 * On CAN (ISO 15765-4), a tester sends requests to 0x7DF, which every
 * emissions-related ECU listens to, or to one ECU's own ID (0x7E0-0x7E7).
 * Each ECU replies from its ID plus 8 (0x7E8-0x7EF). Messages longer than 7
 * bytes travel over ISO-TP (isotp.h).
 *
 * This module turns replies into numbers and text, and numbers into reply
 * bytes for the simulated ECU. Values are integers in display units scaled
 * by 10^decimals, so no floating point is needed: throttle at 23.5% is 235,
 * with one decimal.
 */
#ifndef OBD2_H
#define OBD2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OBD2_SERVICE_CURRENT_DATA 0x01U
#define OBD2_SERVICE_STORED_DTCS  0x03U
#define OBD2_SERVICE_VEHICLE_INFO 0x09U
#define OBD2_REPLY_OFFSET         0x40U /* reply service = request service + 0x40 */

/* Service 01 PIDs. */
#define OBD2_PID_SUPPORTED_01_20 0x00U /* bit mask of the PIDs 01-20 the ECU supports */
#define OBD2_PID_MONITOR_STATUS  0x01U /* warning light and number of trouble codes */
#define OBD2_PID_ENGINE_LOAD     0x04U
#define OBD2_PID_COOLANT_TEMP    0x05U
#define OBD2_PID_ENGINE_SPEED    0x0CU
#define OBD2_PID_VEHICLE_SPEED   0x0DU
#define OBD2_PID_INTAKE_TEMP     0x0FU
#define OBD2_PID_THROTTLE        0x11U
#define OBD2_PID_RUN_TIME        0x1FU

/* Service 09 PIDs. */
#define OBD2_INFO_SUPPORTED 0x00U
#define OBD2_INFO_VIN       0x02U

#define OBD2_VIN_LEN      17U
#define OBD2_DTC_TEXT_LEN 6U /* "P0301" and its terminator */

/* How a PID's reply bytes A, B map to its value. */
typedef enum {
    OBD2_FORMULA_PERCENT,     /* A x 100 / 255, in tenths of a percent */
    OBD2_FORMULA_TEMPERATURE, /* A - 40, degrees Celsius */
    OBD2_FORMULA_RPM,         /* (256 A + B) / 4, revolutions per minute */
    OBD2_FORMULA_BYTE,        /* A */
    OBD2_FORMULA_WORD         /* 256 A + B */
} obd2_formula_t;

/* A service 01 PID whose reply is a single number. */
typedef struct {
    uint8_t pid;
    obd2_formula_t formula;
    uint8_t decimals;  /* value = display value x 10^decimals */
    const char *key;   /* field name in host mode */
    const char *label; /* name shown to people */
    const char *unit;
} obd2_pid_t;

/* The numeric PIDs this project knows, in display order. */
#define OBD2_PID_COUNT 7U
size_t obd2_pid_count(void);
const obd2_pid_t *obd2_pid_at(size_t index);

/* The entry for `pid`, or NULL if it isn't one of them. */
const obd2_pid_t *obd2_pid_find(uint8_t pid);

/* Data bytes after the PID in a reply. */
uint8_t obd2_pid_bytes(const obd2_pid_t *info);

/* The value in `data` (the bytes after the PID). False if there are too
 * few bytes. */
bool obd2_decode(const obd2_pid_t *info, const uint8_t *data, size_t len, int32_t *value);

/* The reply bytes for `value`, which is clamped to what the PID can
 * express. Returns the number of bytes written, or 0 if `size` is too
 * small. */
size_t obd2_encode(const obd2_pid_t *info, int32_t value, uint8_t *out, size_t size);

/* Monitor status (PID 01): whether the warning light (MIL) is on, and how
 * many trouble codes are stored. False if there are too few bytes. */
bool obd2_decode_monitor_status(const uint8_t *data, size_t len, bool *mil_on, uint8_t *dtc_count);

/* A whole service 03 reply (43 N, then 2 bytes per code) into `codes`.
 * Returns the number of codes, at most `max`; -1 if the reply is
 * malformed. */
int32_t obd2_parse_dtcs(const uint8_t *reply, size_t len, uint16_t *codes, size_t max);

/* A trouble code as text: 0x0301 -> "P0301". The top two bits pick the
 * system (P powertrain, C chassis, B body, U network). */
void obd2_dtc_text(uint16_t code, char out[OBD2_DTC_TEXT_LEN]);

/* A whole service 09 PID 02 reply (49 02 01 + 17 characters) into a
 * terminated string. False if it is malformed or holds characters that
 * aren't printable. */
bool obd2_parse_vin(const uint8_t *reply, size_t len, char out[OBD2_VIN_LEN + 1U]);

#endif /* OBD2_H */
