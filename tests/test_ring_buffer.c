#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "ring_buffer.h"
#include "test_support.h"

#define CAP 8U

static ring_buffer_t rb;
static uint8_t storage[CAP];

void setUp(void)
{
    memset(storage, 0, sizeof storage);
    TEST_ASSERT_TRUE(ring_init(&rb, storage, CAP));
}

void tearDown(void)
{
}

/* Start both counters just below UINT32_MAX so the next operations wrap. */
static void set_counters(uint32_t value)
{
    atomic_store(&rb.head, value);
    atomic_store(&rb.tail, value);
}

/* ---- init ---------------------------------------------------------------- */

static void test_init_rejects_bad_arguments(void)
{
    ring_buffer_t r;
    uint8_t buf[12];
    TEST_ASSERT_FALSE(ring_init(&r, NULL, 8U));
    TEST_ASSERT_FALSE(ring_init(&r, buf, 0U));
    TEST_ASSERT_FALSE(ring_init(&r, buf, 1U));
    TEST_ASSERT_FALSE(ring_init(&r, buf, 12U)); /* not a power of two */
    TEST_ASSERT_FALSE(ring_init(NULL, buf, 8U));
}

static void test_new_ring_is_empty(void)
{
    TEST_ASSERT_EQUAL_UINT32(CAP, ring_capacity(&rb));
    TEST_ASSERT_EQUAL_UINT32(0U, ring_used(&rb));
    TEST_ASSERT_EQUAL_UINT32(CAP, ring_free(&rb));
    TEST_ASSERT_TRUE(ring_is_empty(&rb));

    uint8_t b;
    TEST_ASSERT_FALSE(ring_get(&rb, &b));
}

/* ---- single bytes -------------------------------------------------------- */

static void test_put_get_preserves_order(void)
{
    for (uint8_t i = 0; i < 5U; i++) {
        TEST_ASSERT_TRUE(ring_put(&rb, i));
    }
    TEST_ASSERT_EQUAL_UINT32(5U, ring_used(&rb));
    for (uint8_t i = 0; i < 5U; i++) {
        uint8_t b = 0xFF;
        TEST_ASSERT_TRUE(ring_get(&rb, &b));
        TEST_ASSERT_EQUAL_UINT8(i, b);
    }
    TEST_ASSERT_TRUE(ring_is_empty(&rb));
}

static void test_all_capacity_is_usable_and_full_rejects(void)
{
    for (uint8_t i = 0; i < CAP; i++) {
        TEST_ASSERT_TRUE(ring_put(&rb, i));
    }
    TEST_ASSERT_EQUAL_UINT32(0U, ring_free(&rb));
    TEST_ASSERT_FALSE(ring_put(&rb, 0xAA));

    uint8_t b;
    TEST_ASSERT_TRUE(ring_get(&rb, &b));
    TEST_ASSERT_EQUAL_UINT8(0U, b);
    TEST_ASSERT_TRUE(ring_put(&rb, 0xAA)); /* one slot freed */
}

/* ---- bulk ---------------------------------------------------------------- */

static void test_write_truncates_when_full(void)
{
    const uint8_t data[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    TEST_ASSERT_EQUAL_size_t(CAP, ring_write(&rb, data, sizeof data));
    TEST_ASSERT_EQUAL_size_t(0U, ring_write(&rb, data, 1U));

    uint8_t out[12] = {0};
    TEST_ASSERT_EQUAL_size_t(CAP, ring_read(&rb, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, CAP);
}

static void test_write_and_read_across_the_end_of_storage(void)
{
    const uint8_t pre[6] = {0};
    uint8_t sink[6];
    ring_write(&rb, pre, sizeof pre);
    ring_read(&rb, sink, sizeof sink); /* indices now at 6 of 8 */

    const uint8_t data[5] = {10, 11, 12, 13, 14};
    TEST_ASSERT_EQUAL_size_t(5U, ring_write(&rb, data, sizeof data));
    /* Physically split: two bytes at the end, three at the start. */
    TEST_ASSERT_EQUAL_UINT8(10U, storage[6]);
    TEST_ASSERT_EQUAL_UINT8(12U, storage[0]);

    uint8_t out[5] = {0};
    TEST_ASSERT_EQUAL_size_t(5U, ring_read(&rb, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, sizeof data);
}

static void test_read_returns_only_what_is_available(void)
{
    const uint8_t data[3] = {7, 8, 9};
    ring_write(&rb, data, sizeof data);
    uint8_t out[8] = {0};
    TEST_ASSERT_EQUAL_size_t(3U, ring_read(&rb, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, sizeof data);
}

static void test_zero_length_operations_are_noops(void)
{
    uint8_t b = 0;
    TEST_ASSERT_EQUAL_size_t(0U, ring_write(&rb, &b, 0U));
    TEST_ASSERT_EQUAL_size_t(0U, ring_read(&rb, &b, 0U));
    TEST_ASSERT_TRUE(ring_is_empty(&rb));
}

/* ---- counter wrap-around ------------------------------------------------- */

static void test_counters_wrap_past_uint32_max(void)
{
    set_counters(UINT32_MAX - 2U);
    const uint8_t data[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_size_t(6U, ring_write(&rb, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT32(6U, ring_used(&rb)); /* head has wrapped, tail hasn't */
    TEST_ASSERT_EQUAL_UINT32(2U, ring_free(&rb));

    uint8_t out[6] = {0};
    TEST_ASSERT_EQUAL_size_t(6U, ring_read(&rb, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, sizeof data);
    TEST_ASSERT_TRUE(ring_is_empty(&rb));
}

/* ---- zero-copy peek/consume ---------------------------------------------- */

static void test_peek_contiguous_stops_at_end_of_storage(void)
{
    set_counters(5U); /* start index 5: 3 bytes until the end */
    const uint8_t data[6] = {1, 2, 3, 4, 5, 6};
    ring_write(&rb, data, sizeof data);

    const uint8_t *p = NULL;
    size_t n = ring_peek_contiguous(&rb, &p);
    TEST_ASSERT_EQUAL_size_t(3U, n);
    TEST_ASSERT_EQUAL_PTR(&storage[5], p);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, p, 3);

    ring_consume(&rb, n);
    n = ring_peek_contiguous(&rb, &p);
    TEST_ASSERT_EQUAL_size_t(3U, n);
    TEST_ASSERT_EQUAL_PTR(&storage[0], p);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&data[3], p, 3);

    ring_consume(&rb, n);
    TEST_ASSERT_EQUAL_size_t(0U, ring_peek_contiguous(&rb, &p));
}

static void test_peek_does_not_remove(void)
{
    ring_put(&rb, 42U);
    const uint8_t *p = NULL;
    TEST_ASSERT_EQUAL_size_t(1U, ring_peek_contiguous(&rb, &p));
    TEST_ASSERT_EQUAL_UINT32(1U, ring_used(&rb));
}

static void test_consume_never_passes_head(void)
{
    ring_put(&rb, 1U);
    ring_consume(&rb, 5U);
    TEST_ASSERT_TRUE(ring_is_empty(&rb));
    TEST_ASSERT_TRUE(ring_put(&rb, 2U));
    TEST_ASSERT_EQUAL_UINT32(1U, ring_used(&rb));
}

/* ---- concurrency ----------------------------------------------------------
 * A producer thread and a consumer thread stream a known byte sequence through
 * a small ring with varying chunk sizes. The consumer checks every byte. Run
 * under ThreadSanitizer (host-tests-tsan preset) to also catch data races. */

#define STRESS_BYTES 2000000U

static ring_buffer_t stress_rb;
static uint8_t stress_storage[64];

static void *producer(void *arg)
{
    (void)arg;
    uint8_t chunk[13];
    uint32_t next = 0U;
    uint32_t size = 1U;
    while (next < STRESS_BYTES) {
        size = (size % 13U) + 1U; /* 1..13 byte chunks */
        uint32_t n = size;
        if (n > STRESS_BYTES - next) {
            n = STRESS_BYTES - next;
        }
        for (uint32_t i = 0; i < n; i++) {
            chunk[i] = (uint8_t)((next + i) * 7U);
        }
        size_t done = 0U;
        while (done < n) {
            done += ring_write(&stress_rb, &chunk[done], n - done);
        }
        next += n;
    }
    return NULL;
}

static void test_spsc_stress_two_threads(void)
{
    TEST_ASSERT_TRUE(ring_init(&stress_rb, stress_storage, sizeof stress_storage));

    pthread_t thread;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&thread, NULL, producer, NULL));

    uint32_t expected = 0U;
    uint32_t errors = 0U;
    uint8_t buf[17];
    while (expected < STRESS_BYTES) {
        size_t n = ring_read(&stress_rb, buf, sizeof buf);
        for (size_t i = 0; i < n; i++) {
            if (buf[i] != (uint8_t)(expected * 7U)) {
                errors++;
            }
            expected++;
        }
    }
    pthread_join(thread, NULL);

    TEST_ASSERT_EQUAL_UINT32(0U, errors);
    TEST_ASSERT_TRUE(ring_is_empty(&stress_rb));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_init_rejects_bad_arguments);
    UNIT_TEST(test_new_ring_is_empty);
    UNIT_TEST(test_put_get_preserves_order);
    UNIT_TEST(test_all_capacity_is_usable_and_full_rejects);
    UNIT_TEST(test_write_truncates_when_full);
    UNIT_TEST(test_write_and_read_across_the_end_of_storage);
    UNIT_TEST(test_read_returns_only_what_is_available);
    UNIT_TEST(test_zero_length_operations_are_noops);
    UNIT_TEST(test_counters_wrap_past_uint32_max);
    UNIT_TEST(test_peek_contiguous_stops_at_end_of_storage);
    UNIT_TEST(test_peek_does_not_remove);
    UNIT_TEST(test_consume_never_passes_head);
    UNIT_TEST(test_spsc_stress_two_threads);
    return UNITY_END();
}
