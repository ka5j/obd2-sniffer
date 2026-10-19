/*
 * Test traffic: sends a numbered series of CAN frames, for checking the
 * capture path end to end in loopback.
 *
 * Every frame has ID CAN_GEN_ID and 8 data bytes: a 32-bit sequence number
 * (little-endian) followed by its bitwise inverse. A receiver can check that
 * every frame arrived, in order and undamaged.
 */
#ifndef CAN_GEN_H
#define CAN_GEN_H

#include <stdbool.h>
#include <stdint.h>

#define CAN_GEN_ID        0x123U
#define CAN_GEN_MAX_COUNT 10000000U

/* Start sending `count` frames, `per_second` of them per second (0: as fast
 * as the bus allows). Replaces a series already running. False if CAN is
 * not in loopback or the count is out of range. */
bool can_gen_start(uint32_t count, uint32_t per_second);

void can_gen_stop(void);

bool can_gen_running(void);

/* Frames sent in the current (or last) series, and its length. */
uint32_t can_gen_sent(void);
uint32_t can_gen_count(void);

/* Main loop: hands the next frames to the controller when they are due. */
void can_gen_task(void);

#endif /* CAN_GEN_H */
