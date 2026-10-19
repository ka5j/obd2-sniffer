#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "frame_queue.h"
#include "test_support.h"

#define CAPACITY 256U /* bytes: 12 frames of 20 bytes, 16 bytes unused */

static frame_queue_t q;
static uint8_t storage[CAPACITY];

void setUp(void)
{
    TEST_ASSERT_TRUE(frame_queue_init(&q, storage, CAPACITY));
}

void tearDown(void)
{
}

static can_frame_t make_frame(uint32_t n)
{
    can_frame_t f = {
        .timestamp_us = n * 100U,
        .id = n & 0x7FFU,
        .dlc = (uint8_t)(n % 9U),
        .flags = (uint8_t)(n & 1U),
    };
    for (uint32_t i = 0U; i < CAN_MAX_DLC; i++) {
        f.data[i] = (uint8_t)(n + i);
    }
    return f;
}

/* Field by field: memcmp would also compare the struct's padding bytes,
 * whose values are unspecified. */
static bool same_frame(const can_frame_t *a, const can_frame_t *b)
{
    return (a->timestamp_us == b->timestamp_us) && (a->id == b->id) && (a->dlc == b->dlc) &&
           (a->flags == b->flags) && (memcmp(a->data, b->data, CAN_MAX_DLC) == 0);
}

static void test_init_rejects_bad_arguments(void)
{
    frame_queue_t other;
    uint8_t buf[64];
    TEST_ASSERT_FALSE(frame_queue_init(NULL, buf, 64U));
    TEST_ASSERT_FALSE(frame_queue_init(&other, buf, 16U)); /* smaller than a frame */
    TEST_ASSERT_FALSE(frame_queue_init(&other, buf, 48U)); /* not a power of two */
    TEST_ASSERT_FALSE(frame_queue_init(&other, NULL, 64U));
    TEST_ASSERT_TRUE(frame_queue_init(&other, buf, 64U));
}

static void test_frames_come_out_whole_and_in_order(void)
{
    TEST_ASSERT_EQUAL_UINT32(CAPACITY / sizeof(can_frame_t), frame_queue_capacity(&q));
    for (uint32_t n = 0U; n < 5U; n++) {
        can_frame_t f = make_frame(n);
        TEST_ASSERT_TRUE(frame_queue_push(&q, &f));
    }
    TEST_ASSERT_EQUAL_UINT32(5U, frame_queue_count(&q));
    for (uint32_t n = 0U; n < 5U; n++) {
        can_frame_t expected = make_frame(n);
        can_frame_t got;
        TEST_ASSERT_TRUE(frame_queue_pop(&q, &got));
        TEST_ASSERT_TRUE(same_frame(&expected, &got));
    }
    can_frame_t got;
    TEST_ASSERT_FALSE(frame_queue_pop(&q, &got)); /* empty */
}

static void test_full_queue_rejects_without_storing(void)
{
    uint32_t cap = frame_queue_capacity(&q);
    for (uint32_t n = 0U; n < cap; n++) {
        can_frame_t f = make_frame(n);
        TEST_ASSERT_TRUE(frame_queue_push(&q, &f));
    }
    can_frame_t extra = make_frame(99U);
    TEST_ASSERT_FALSE(frame_queue_push(&q, &extra));
    TEST_ASSERT_EQUAL_UINT32(cap, frame_queue_count(&q));

    can_frame_t got;
    TEST_ASSERT_TRUE(frame_queue_pop(&q, &got));
    TEST_ASSERT_EQUAL_UINT32(0U, got.timestamp_us); /* the oldest, not the rejected one */
}

/* 256 isn't a multiple of 20, so frames straddle the end of the storage as
 * the queue is used; they must still come out intact. */
static void test_frames_straddling_the_end_stay_intact(void)
{
    uint32_t next_in = 0U;
    uint32_t next_out = 0U;
    for (uint32_t round = 0U; round < 100U; round++) {
        for (uint32_t k = 0U; k < 7U; k++) {
            can_frame_t f = make_frame(next_in);
            TEST_ASSERT_TRUE(frame_queue_push(&q, &f));
            next_in++;
        }
        for (uint32_t k = 0U; k < 7U; k++) {
            can_frame_t expected = make_frame(next_out);
            can_frame_t got;
            TEST_ASSERT_TRUE(frame_queue_pop(&q, &got));
            TEST_ASSERT_TRUE(same_frame(&expected, &got));
            next_out++;
        }
    }
}

/* ---- two threads -----------------------------------------------------------
 * The receive interrupt (producer) and the main loop (consumer) modelled as
 * two threads; run under ThreadSanitizer (host-tests-tsan) for data races. */

#define STRESS_FRAMES 200000U

static frame_queue_t stress_q;
static uint8_t stress_storage[512];

static void *producer(void *arg)
{
    (void)arg;
    for (uint32_t n = 0U; n < STRESS_FRAMES; n++) {
        can_frame_t f = make_frame(n);
        while (!frame_queue_push(&stress_q, &f)) {
        }
    }
    return NULL;
}

static void test_spsc_stress_two_threads(void)
{
    TEST_ASSERT_TRUE(frame_queue_init(&stress_q, stress_storage, sizeof stress_storage));
    pthread_t thread;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&thread, NULL, producer, NULL));

    uint32_t errors = 0U;
    for (uint32_t n = 0U; n < STRESS_FRAMES;) {
        can_frame_t got;
        if (frame_queue_pop(&stress_q, &got)) {
            can_frame_t expected = make_frame(n);
            errors += same_frame(&expected, &got) ? 0U : 1U;
            n++;
        }
    }
    pthread_join(thread, NULL);
    TEST_ASSERT_EQUAL_UINT32(0U, errors);
    TEST_ASSERT_EQUAL_UINT32(0U, frame_queue_count(&stress_q));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_init_rejects_bad_arguments);
    UNIT_TEST(test_frames_come_out_whole_and_in_order);
    UNIT_TEST(test_full_queue_rejects_without_storing);
    UNIT_TEST(test_frames_straddling_the_end_stay_intact);
    UNIT_TEST(test_spsc_stress_two_threads);
    return UNITY_END();
}
