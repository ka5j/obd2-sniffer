/*
 * CAN bit timing.
 *
 * A CAN controller divides each bit into time quanta (tq): one for
 * synchronisation, then segment 1, then segment 2. The bus level is sampled
 * between the two segments, at the "sample point". Sampling late in the bit
 * gives the signal time to settle across the whole bus, so automotive
 * networks sample at about 87.5% (CiA 301, SAE J2284).
 *
 * can_timing_calc() finds settings for the bxCAN controller: the clock is
 * divided by `prescaler` into time quanta, 8-25 of them per bit, with
 * segment 1 of 1-16 tq and segment 2 of 1-8 tq. Among the exact solutions
 * it picks the sample point closest to 87.5%, preferring more time quanta
 * (finer resynchronisation) when two are equally close.
 */
#ifndef CAN_TIMING_H
#define CAN_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#define CAN_TIMING_SAMPLE_POINT_TARGET 875U /* per mille */

typedef struct {
    uint32_t prescaler; /* clock divider, 1-1024 */
    uint32_t bs1;       /* segment 1 (including propagation segment), tq */
    uint32_t bs2;       /* segment 2, tq */
    uint32_t sjw;       /* resynchronisation jump width, tq */
} can_timing_t;

/* False if no setting gives exactly `bitrate` from `clock_hz`. */
bool can_timing_calc(uint32_t clock_hz, uint32_t bitrate, can_timing_t *out);

/* Time quanta per bit: 1 + bs1 + bs2. */
uint32_t can_timing_tq_per_bit(const can_timing_t *t);

/* Sample point in per mille, e.g. 867 for 86.7%. */
uint32_t can_timing_sample_point(const can_timing_t *t);

#endif /* CAN_TIMING_H */
