/*
 * CAN1 driver: capture every frame on the bus, timestamped.
 *
 * Receive: the controller's FIFO raises an interrupt for each frame. The
 * interrupt stamps it with the microsecond timebase and puts it in a
 * lock-free queue (lib/frame_queue); the main loop takes frames out with
 * can_drv_receive(). The interrupt does nothing else, so it is short.
 *
 * Modes: off at start-up. Loopback is the controller's silent loopback:
 * frames it sends are received internally and nothing reaches the pins, so
 * the whole path can be tested on the board alone. Sending is only allowed
 * in loopback.
 */
#ifndef CAN_DRV_H
#define CAN_DRV_H

#include <stdbool.h>
#include <stdint.h>

#include "can_frame.h"

typedef enum {
    CAN_DRV_OFF,
    CAN_DRV_LOOPBACK,
} can_drv_mode_t;

typedef struct {
    uint32_t rx_frames;      /* received and queued */
    uint32_t rx_dropped;     /* received while the queue was full */
    uint32_t rx_overruns;    /* lost in hardware: the FIFO was full */
    uint32_t tx_frames;      /* handed to the controller for sending */
    uint32_t isr_max_cycles; /* longest receive interrupt, CPU cycles */
    uint32_t tx_errors;      /* transmit error counter (TEC) */
    uint32_t rx_errors;      /* receive error counter (REC) */
    bool error_passive;
    bool bus_off;
} can_drv_stats_t;

/* Pins, clock, interrupt and the receive queue. Leaves CAN off. */
bool can_drv_init(void);

/* Start silent loopback at `bitrate` bit/s, clearing the statistics. False
 * if no exact bit timing exists for it or the controller didn't start. */
bool can_drv_start_loopback(uint32_t bitrate);

void can_drv_stop(void);

can_drv_mode_t can_drv_mode(void);
const char *can_drv_mode_name(can_drv_mode_t mode);
uint32_t can_drv_bitrate(void);

/* Send one frame. False if CAN is not in loopback or all three transmit
 * mailboxes are busy. */
bool can_drv_send(const can_frame_t *frame);

/* Free transmit mailboxes (0-3). */
uint32_t can_drv_tx_free(void);

/* Next received frame, oldest first. False if none. Main loop only. */
bool can_drv_receive(can_frame_t *frame);

void can_drv_get_stats(can_drv_stats_t *out);

/* Frames waiting in the receive queue, and its size. */
uint32_t can_drv_queued(void);
uint32_t can_drv_queue_capacity(void);

#endif /* CAN_DRV_H */
