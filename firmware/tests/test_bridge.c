// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file test_bridge.c
 * @brief Host-native Unity tests for UART ring bridge operations.
 */

#include "unity.h"

#include "uart/bridge.h"

#include <string.h>

static uint8_t rx_snapshot[UART_BRIDGE_RX_SNAPSHOT_SIZE];

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} bridge_reader_t;

typedef struct {
    uint8_t data[2048];
    size_t length;
    bool limit_acceptance;
    uint32_t max_acceptance;
    ring_buffer_t *overwrite_ring;
} bridge_writer_t;

static uint32_t bridge_reader(void *context, uint8_t *data, uint32_t length)
{
    bridge_reader_t *reader = context;
    size_t available = reader->length - reader->offset;
    size_t count = (available < length) ? available : length;

    memcpy(data, reader->data + reader->offset, count);
    reader->offset += count;
    return (uint32_t)count;
}

static uint32_t bridge_writer(void *context, const uint8_t *data, uint32_t length)
{
    bridge_writer_t *writer = context;

    if (writer->limit_acceptance && length > writer->max_acceptance) {
        length = writer->max_acceptance;
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT(sizeof(writer->data), writer->length + length);
    memcpy(writer->data + writer->length, data, length);
    writer->length += length;
    if (writer->overwrite_ring != NULL) {
        ring_buffer_t *ring = writer->overwrite_ring;
        memset(ring->storage, 0xff, ring->size);
        ring_buffer_produce_external(ring, ring->size + 2u);
        writer->overwrite_ring = NULL;
    }
    return length;
}

static bool bridge_change_stats_sequence(void *context, uint32_t consumer_sequence)
{
    volatile uint32_t *sequence = context;
    (void)consumer_sequence;
    *sequence += 2u;
    return true;
}

static bool bridge_snapshot_current(void *context, uint32_t consumer_sequence)
{
    (void)consumer_sequence;
    return *(const bool *)context;
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_fill_tx_honors_capacity(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];
    uint8_t output[8];
    bridge_reader_t reader = {.data = (const uint8_t *)"abcdef", .length = 6u};

    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(4u, uart_bridge_fill_tx(&ring, 4u, bridge_reader, &reader));
    TEST_ASSERT_EQUAL_UINT(4u, ring_buffer_read(&ring, output, sizeof(output)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"abcd", output, 4u);
}

void test_drain_rx_commits_valid_snapshot(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];
    bool snapshot_current = true;
    volatile uint32_t stats_sequence = 0u;
    bridge_writer_t writer = {0};

    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&ring, (const uint8_t *)"hello", 5u));
    TEST_ASSERT_EQUAL_UINT(5u, uart_bridge_drain_rx(&ring, 0u, bridge_writer, &writer, rx_snapshot,
                                                    sizeof(rx_snapshot), bridge_snapshot_current, &snapshot_current,
                                                    &stats_sequence));
    TEST_ASSERT_EQUAL_UINT(5u, writer.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"hello", writer.data, writer.length);
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&ring));
}

void test_drain_rx_rejects_stale_snapshot(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];
    bool snapshot_current = false;
    volatile uint32_t stats_sequence = 0u;
    bridge_writer_t writer = {0};

    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_write(&ring, (const uint8_t *)"old", 3u));
    TEST_ASSERT_EQUAL_UINT(0u, uart_bridge_drain_rx(&ring, 0u, bridge_writer, &writer, rx_snapshot,
                                                    sizeof(rx_snapshot), bridge_snapshot_current, &snapshot_current,
                                                    &stats_sequence));
    TEST_ASSERT_EQUAL_UINT(0u, writer.length);
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_occupancy(&ring));
}

void test_drain_rx_requires_caller_snapshot_storage(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];
    bool current = true;
    volatile uint32_t sequence = 0u;
    bridge_writer_t writer = {0};

    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_write(&ring, (const uint8_t *)"abc", 3u));
    TEST_ASSERT_EQUAL_UINT(0u, uart_bridge_drain_rx(&ring, 0u, bridge_writer, &writer, NULL,
                                                    sizeof(rx_snapshot), bridge_snapshot_current, &current,
                                                    &sequence));
    TEST_ASSERT_EQUAL_UINT(0u, writer.length);
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_occupancy(&ring));
}

void test_drain_rx_retires_only_partial_acceptance_and_preserves_zero_acceptance(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];
    bool current = true;
    volatile uint32_t sequence = 0u;
    bridge_writer_t writer = {.limit_acceptance = true, .max_acceptance = 2u};
    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(6u, ring_buffer_write(&ring, (const uint8_t *)"abcdef", 6u));
    TEST_ASSERT_EQUAL_UINT(2u, uart_bridge_drain_rx(&ring, 0u, bridge_writer, &writer, rx_snapshot,
                                                sizeof(rx_snapshot), bridge_snapshot_current, &current, &sequence));
    TEST_ASSERT_EQUAL_UINT(4u, ring_buffer_occupancy(&ring));
    writer.max_acceptance = 0u;
    TEST_ASSERT_EQUAL_UINT(0u, uart_bridge_drain_rx(&ring, 0u, bridge_writer, &writer, rx_snapshot,
                                                sizeof(rx_snapshot), bridge_snapshot_current, &current, &sequence));
    TEST_ASSERT_EQUAL_UINT(2u, ring.consumer);
    TEST_ASSERT_EQUAL_UINT(4u, ring_buffer_occupancy(&ring));
    writer.limit_acceptance = false;
    TEST_ASSERT_EQUAL_UINT(4u, uart_bridge_drain_rx(&ring, 0u, bridge_writer, &writer, rx_snapshot,
                                                sizeof(rx_snapshot), bridge_snapshot_current, &current, &sequence));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"abcdef", writer.data, 6u);
}

void test_bridge_preserves_order_across_wrapped_tx_and_rx_spans(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];
    uint8_t discarded[6];
    bool current = true;
    volatile uint32_t sequence = 0u;
    bridge_reader_t reader = {.data = (const uint8_t *)"wrapped", .length = 7u};
    bridge_writer_t writer = {0};
    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(6u, ring_buffer_write(&ring, (const uint8_t *)"unused", 6u));
    TEST_ASSERT_EQUAL_UINT(6u, ring_buffer_read(&ring, discarded, sizeof(discarded)));
    TEST_ASSERT_EQUAL_UINT(7u, uart_bridge_fill_tx(&ring, 7u, bridge_reader, &reader));
    TEST_ASSERT_EQUAL_UINT(7u, uart_bridge_drain_rx(&ring, 7u, bridge_writer, &writer, rx_snapshot,
                                                sizeof(rx_snapshot), bridge_snapshot_current, &current, &sequence));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(reader.data, writer.data, 7u);
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&ring));
}

void test_drain_rx_rejects_odd_or_changed_stats_sequence_before_writing(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];
    bool current = true;
    uint32_t sequence = 1u;
    bridge_writer_t writer = {0};
    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_write(&ring, (const uint8_t *)"old", 3u));
    TEST_ASSERT_EQUAL_UINT(0u, uart_bridge_drain_rx(&ring, 0u, bridge_writer, &writer, rx_snapshot,
                                                sizeof(rx_snapshot), bridge_snapshot_current, &current, &sequence));
    sequence = 0u;
    TEST_ASSERT_EQUAL_UINT(0u, uart_bridge_drain_rx(&ring, 0u, bridge_writer, &writer, rx_snapshot,
                                                sizeof(rx_snapshot), bridge_change_stats_sequence, &sequence,
                                                &sequence));
    TEST_ASSERT_EQUAL_UINT(0u, writer.length);
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_occupancy(&ring));
}

void test_drain_rx_accounts_overflow_after_snapshot_was_accepted(void)
{
    ring_buffer_t ring;
    uint8_t storage[8];
    bool current = true;
    volatile uint32_t sequence = 0u;
    bridge_writer_t writer = {.overwrite_ring = &ring, .limit_acceptance = true, .max_acceptance = 2u};
    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(6u, ring_buffer_write(&ring, (const uint8_t *)"abcdef", 6u));
    TEST_ASSERT_EQUAL_UINT(2u, uart_bridge_drain_rx(&ring, 0u, bridge_writer, &writer, rx_snapshot,
                                                sizeof(rx_snapshot), bridge_snapshot_current, &current, &sequence));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"ab", writer.data, 2u);
    TEST_ASSERT_EQUAL_UINT(6u, ring_buffer_overflow_count(&ring));
    TEST_ASSERT_EQUAL_UINT(8u, ring.consumer);
    TEST_ASSERT_EQUAL_UINT(8u, ring_buffer_occupancy(&ring));
}

void test_drain_rx_chunks_snapshots_and_honors_capacity_across_chunks(void)
{
    ring_buffer_t ring;
    uint8_t storage[2048];
    uint8_t input[1500];
    bool current = true;
    volatile uint32_t sequence = 0u;
    bridge_writer_t writer = {0};
    for (size_t index = 0u; index < sizeof(input); ++index) {
        input[index] = (uint8_t)index;
    }
    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL_UINT(sizeof(input), ring_buffer_write(&ring, input, sizeof(input)));
    TEST_ASSERT_EQUAL_UINT(1300u, uart_bridge_drain_rx(&ring, 1300u, bridge_writer, &writer, rx_snapshot,
                                                   sizeof(rx_snapshot), bridge_snapshot_current, &current,
                                                   &sequence));
    TEST_ASSERT_EQUAL_UINT(200u, ring_buffer_occupancy(&ring));
    TEST_ASSERT_EQUAL_UINT(200u, uart_bridge_drain_rx(&ring, 0u, bridge_writer, &writer, rx_snapshot,
                                                   sizeof(rx_snapshot), bridge_snapshot_current, &current,
                                                   &sequence));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, writer.data, sizeof(input));
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&ring));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fill_tx_honors_capacity);
    RUN_TEST(test_drain_rx_commits_valid_snapshot);
    RUN_TEST(test_drain_rx_rejects_stale_snapshot);
    RUN_TEST(test_drain_rx_requires_caller_snapshot_storage);
    RUN_TEST(test_drain_rx_retires_only_partial_acceptance_and_preserves_zero_acceptance);
    RUN_TEST(test_bridge_preserves_order_across_wrapped_tx_and_rx_spans);
    RUN_TEST(test_drain_rx_rejects_odd_or_changed_stats_sequence_before_writing);
    RUN_TEST(test_drain_rx_accounts_overflow_after_snapshot_was_accepted);
    RUN_TEST(test_drain_rx_chunks_snapshots_and_honors_capacity_across_chunks);
    return UNITY_END();
}