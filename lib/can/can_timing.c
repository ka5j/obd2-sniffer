#include "can/can_timing.h"

#define TQ_MIN        8U
#define TQ_MAX        25U
#define BS1_MAX       16U
#define PRESCALER_MAX 1024U

uint32_t can_timing_tq_per_bit(const can_timing_t *t)
{
    return 1U + t->bs1 + t->bs2;
}

uint32_t can_timing_sample_point(const can_timing_t *t)
{
    /* The sample point is at the end of segment 1. */
    return ((1U + t->bs1) * 1000U) / can_timing_tq_per_bit(t);
}

/* Segments for `tq` quanta per bit with the sample point nearest the
 * target. For 8-25 quanta segment 2 comes out as 1-3 quanta, always within
 * bxCAN's 1-8; only segment 1 can exceed its limit (above 19 quanta). */
static bool split_bit(uint32_t tq, can_timing_t *t)
{
    /* Quanta up to the sample point, rounded to the nearest. */
    uint32_t before_sample = ((tq * CAN_TIMING_SAMPLE_POINT_TARGET) + 500U) / 1000U;
    t->bs1 = before_sample - 1U;
    t->bs2 = tq - before_sample;
    /* The widest jump segment 2 allows: tolerates the most clock mismatch
     * between nodes (at most 3 here, within bxCAN's limit of 4). */
    t->sjw = t->bs2;
    return t->bs1 <= BS1_MAX;
}

static uint32_t distance(uint32_t a, uint32_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

bool can_timing_calc(uint32_t clock_hz, uint32_t bitrate, can_timing_t *out)
{
    bool found = false;
    uint32_t best_error = 0U;

    /* At least TQ_MIN quanta per bit (this also rules out a zero clock). */
    if ((bitrate == 0U) || (bitrate > (clock_hz / TQ_MIN))) {
        return false;
    }
    /* From the most quanta down: on a tie, the first (finest) one stays. */
    for (uint32_t tq = TQ_MAX; tq >= TQ_MIN; tq--) {
        /* 64-bit: bitrate * tq can exceed 32 bits for clocks above 1.3 GHz. */
        uint64_t quanta_per_s = (uint64_t)bitrate * tq;
        if (((uint64_t)clock_hz % quanta_per_s) != 0U) {
            continue;
        }
        can_timing_t candidate = {.prescaler = (uint32_t)((uint64_t)clock_hz / quanta_per_s)};
        if ((candidate.prescaler > PRESCALER_MAX) || !split_bit(tq, &candidate)) {
            continue;
        }
        uint32_t error =
            distance(can_timing_sample_point(&candidate), CAN_TIMING_SAMPLE_POINT_TARGET);
        if (!found || (error < best_error)) {
            *out = candidate;
            best_error = error;
            found = true;
        }
    }
    return found;
}
