/**
 * @file test_ring_buffer.c
 * @brief Host-native Unity tests for the PicoUart ring buffer.
 */

#include "unity.h"

#include "uart/ring_buffer/ring_buffer.h"

#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

void test_init_rejects_null_and_non_power_of_two(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];

    TEST_ASSERT_FALSE(ring_buffer_init(NULL, storage, sizeof(storage)));
    TEST_ASSERT_FALSE(ring_buffer_init(&ring, NULL, sizeof(storage)));
    TEST_ASSERT_FALSE(ring_buffer_init(&ring, storage, 0u));
    TEST_ASSERT_FALSE(ring_buffer_init(&ring, storage, 6u));
    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&ring));
    TEST_ASSERT_EQUAL_UINT(8u, ring_buffer_free_space(&ring));
}

void test_write_read_round_trip(void)
{
    ring_buffer_t ring;
    uint8_t storage[16];
    uint8_t input[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint8_t output[10] = {0};

    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(10u, ring_buffer_write(&ring, input, sizeof(input)));
    TEST_ASSERT_EQUAL_UINT(10u, ring_buffer_occupancy(&ring));
    TEST_ASSERT_EQUAL_UINT(10u, ring_buffer_high_watermark(&ring));
    TEST_ASSERT_EQUAL_UINT(10u, ring_buffer_read(&ring, output, sizeof(output)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, output, sizeof(input));
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&ring));
}

void test_contiguous_spans_wrap(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];
    uint8_t scratch[8];
    ring_buffer_span_t span;

    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&ring, (const uint8_t *)"abcde", 5u));
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_read(&ring, scratch, 3u));

    span = ring_buffer_write_span(&ring);
    TEST_ASSERT_EQUAL_UINT(3u, span.length);
    memcpy(span.data, "fgh", 3u);
    TEST_ASSERT_TRUE(ring_buffer_commit_produced(&ring, 3u));

    span = ring_buffer_read_span(&ring);
    TEST_ASSERT_TRUE(span.length >= 1u);
    TEST_ASSERT_TRUE(ring_buffer_commit_consumed(&ring, span.length));
}

void test_overwrite_recovery_on_read_span(void)
{
    ring_buffer_t ring;
    uint8_t storage[4];
    uint8_t output[4];
    size_t recovered;

    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(4u, ring_buffer_write(&ring, (const uint8_t *)"abcd", 4u));
    ring_buffer_write_byte_overwrite(&ring, (uint8_t)'e');
    ring_buffer_write_byte_overwrite(&ring, (uint8_t)'f');

    TEST_ASSERT_EQUAL_UINT(2u, ring_buffer_pending_overflow(&ring));
    recovered = ring_buffer_recover_overflow(&ring);
    TEST_ASSERT_EQUAL_UINT(2u, recovered);
    TEST_ASSERT_EQUAL_UINT(2u, ring_buffer_overflow_count(&ring));
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_pending_overflow(&ring));
    TEST_ASSERT_EQUAL_UINT(4u, ring_buffer_read(&ring, output, sizeof(output)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"cdef", output, 4u);
}

void test_read_span_recovers_before_returning_data(void)
{
    ring_buffer_t ring;
    uint8_t storage[4];
    ring_buffer_span_t span;

    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(4u, ring_buffer_write(&ring, (const uint8_t *)"wxyz", 4u));
    ring_buffer_produce_external(&ring, 3u);

    /* read_span must recover overwritten bytes before exposing a window. */
    span = ring_buffer_read_span(&ring);
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_pending_overflow(&ring));
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_overflow_count(&ring));
    TEST_ASSERT_TRUE(span.length > 0u);
    TEST_ASSERT_TRUE(ring_buffer_commit_consumed(&ring, span.length));
}

void test_commit_rejects_oversized_counts(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];
    ring_buffer_span_t span;

    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(4u, ring_buffer_write(&ring, (const uint8_t *)"1234", 4u));

    span = ring_buffer_write_span(&ring);
    TEST_ASSERT_TRUE(span.length > 0u);
    TEST_ASSERT_FALSE(ring_buffer_commit_produced(&ring, span.length + 1u));

    span = ring_buffer_read_span(&ring);
    TEST_ASSERT_TRUE(span.length > 0u);
    TEST_ASSERT_FALSE(ring_buffer_commit_consumed(&ring, span.length + 1u));
}

void test_builtin_self_check(void)
{
    TEST_ASSERT_TRUE(ring_buffer_self_check());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_rejects_null_and_non_power_of_two);
    RUN_TEST(test_write_read_round_trip);
    RUN_TEST(test_contiguous_spans_wrap);
    RUN_TEST(test_overwrite_recovery_on_read_span);
    RUN_TEST(test_read_span_recovers_before_returning_data);
    RUN_TEST(test_commit_rejects_oversized_counts);
    RUN_TEST(test_builtin_self_check);
    return UNITY_END();
}
