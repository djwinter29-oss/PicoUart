/**
 * @file test_bridge.c
 * @brief Host-native Unity tests for UART ring bridge operations.
 */

#include "unity.h"

#include "uart/bridge.h"

#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} bridge_reader_t;

typedef struct {
    uint8_t data[16];
    size_t length;
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

    memcpy(writer->data + writer->length, data, length);
    writer->length += length;
    return length;
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
    TEST_ASSERT_EQUAL_UINT(5u,
                           uart_bridge_drain_rx(&ring,
                                                0u,
                                                bridge_writer,
                                                &writer,
                                                bridge_snapshot_current,
                                                &snapshot_current,
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
    TEST_ASSERT_EQUAL_UINT(0u,
                           uart_bridge_drain_rx(&ring,
                                                0u,
                                                bridge_writer,
                                                &writer,
                                                bridge_snapshot_current,
                                                &snapshot_current,
                                                &stats_sequence));
    TEST_ASSERT_EQUAL_UINT(0u, writer.length);
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_occupancy(&ring));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fill_tx_honors_capacity);
    RUN_TEST(test_drain_rx_commits_valid_snapshot);
    RUN_TEST(test_drain_rx_rejects_stale_snapshot);
    return UNITY_END();
}