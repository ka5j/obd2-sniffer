/*
 * OBD2 on the board: the diagnostic tester (lib/obd/obd_client) and, in
 * loopback, a simulated engine ECU for it to talk to (lib/obd/ecu_node).
 *
 * In loopback the controller receives every frame it sends, and nothing
 * reaches the pins, so there is no car to answer. The board plays one:
 * requests the tester sends come back to the simulated ECU, and its replies
 * come back to the tester, all through the real CAN controller. The frames
 * are captured and streamed like any others.
 *
 * Frames for these two reach this module through their own queue
 * (can_drv_receive_obd()), so they are handled at once even when the serial
 * link is busy or streaming is off.
 */
#ifndef OBD_H
#define OBD_H

#include <stdbool.h>
#include <stdint.h>

#include "obd/obd_client.h"

void obd_init(void);

/* Main loop: pass received frames to the tester and the ECU, and send what
 * they have for the bus. Never waits. */
void obd_task(uint32_t now_ms);

/* Start the tester. False if CAN isn't in loopback. */
bool obd_start(void);

void obd_stop(void);

/* The simulated ECU answers while CAN is in loopback. */
bool obd_ecu_on(void);

/* The tester's latest answers and counters. */
const obd_client_t *obd_tester(void);

#endif /* OBD_H */
