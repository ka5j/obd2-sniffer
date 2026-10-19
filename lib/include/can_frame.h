/*
 * One CAN frame as captured from the bus.
 *
 * A classic CAN frame carries an identifier (11-bit standard or 29-bit
 * extended), a data length code (0-8) and up to 8 data bytes. A remote frame
 * (RTR) asks another node to send data and carries none itself. The capture
 * adds the time it was received, in microseconds from a free-running counter.
 */
#ifndef CAN_FRAME_H
#define CAN_FRAME_H

#include <stdint.h>

#define CAN_MAX_DLC 8U

/* can_frame_t.flags */
#define CAN_FLAG_EXTENDED 0x01U /* 29-bit identifier */
#define CAN_FLAG_RTR      0x02U /* remote frame: no data */

typedef struct {
    uint32_t timestamp_us; /* receive time; wraps every 71.6 minutes */
    uint32_t id;
    uint8_t dlc; /* 0-8 */
    uint8_t flags;
    uint8_t data[CAN_MAX_DLC];
} can_frame_t;

#endif /* CAN_FRAME_H */
