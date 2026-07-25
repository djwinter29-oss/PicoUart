/**
 * @file ring_buffer.c
 * @brief Fixed-size ring-buffer helpers for PicoUart DMA and USB bridging.
 */

#include "uart/ring_buffer/ring_buffer.h"

#include "hardware/sync.h"

#include <string.h>

static uint32_t ring_buffer_min(uint32_t left, uint32_t right)
{
    return (left < right) ? left : right;
}

static bool ring_buffer_size_is_valid(size_t size)
{
    return (size != 0u) && (size <= UINT32_MAX) && ((size & (size - 1u)) == 0u);
}

static void ring_buffer_reset(ring_buffer_t *ring)
{
    ring->producer = 0u;
    ring->consumer = 0u;
    ring->high_watermark = 0u;
    ring->overflow_count = 0u;
    ring->producer_reserved_sequence = 0u;
    ring->producer_reserved_count = 0u;
    ring->consumer_reserved_sequence = 0u;
    ring->consumer_reserved_count = 0u;
}

static uint32_t ring_buffer_available(const ring_buffer_t *ring)
{
    uint32_t producer = ring->producer;
    uint32_t consumer = ring->consumer;
    uint32_t available;

    __dmb();
    available = producer - consumer;

    return (available > ring->size) ? ring->size : available;
}

static uint32_t ring_buffer_pending_overflow_internal(const ring_buffer_t *ring)
{
    uint32_t producer = ring->producer;
    uint32_t consumer = ring->consumer;
    uint32_t available;

    __dmb();
    available = producer - consumer;
    return (available > ring->size) ? (available - ring->size) : 0u;
}

static void ring_buffer_update_high_watermark(ring_buffer_t *ring)
{
    uint32_t available = ring_buffer_available(ring);

    if (available > ring->high_watermark) {
        ring->high_watermark = available;
    }
}

bool ring_buffer_init(ring_buffer_t *ring, uint8_t *storage, size_t size)
{
    if ((ring == NULL) || (storage == NULL) || !ring_buffer_size_is_valid(size)) {
        return false;
    }

    ring->storage = storage;
    ring->size = (uint32_t)size;
    ring->mask = (uint32_t)(size - 1u);
    ring_buffer_reset(ring);
    return true;
}

bool ring_buffer_self_check(void)
{
    uint8_t storage[8];
    uint8_t input[8] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
    uint8_t output[8];
    ring_buffer_t ring;

    if (!ring_buffer_init(&ring, storage, sizeof(storage)) ||
        (ring_buffer_write(&ring, input, sizeof(input)) != sizeof(input))) {
        return false;
    }

    ring_buffer_write_byte_overwrite(&ring, 8u);
    if (ring_buffer_pending_overflow(&ring) != 1u) {
        return false;
    }

    if ((ring_buffer_recover_overflow(&ring) != 1u) ||
        (ring_buffer_overflow_count(&ring) != 1u) ||
        (ring_buffer_pending_overflow(&ring) != 0u) ||
        (ring_buffer_read(&ring, output, sizeof(output)) != sizeof(output))) {
        return false;
    }

    for (size_t index = 0u; index < sizeof(output); ++index) {
        if (output[index] != (uint8_t)(index + 1u)) {
            return false;
        }
    }

    if (!ring_buffer_init(&ring, storage, sizeof(storage)) ||
        (ring_buffer_write(&ring, input, 6u) != 6u) ||
        (ring_buffer_read(&ring, output, 4u) != 4u) ||
        (ring_buffer_write_span(&ring).length != 2u) ||
        ring_buffer_commit_produced(&ring, 3u)) {
        return false;
    }

    if (!ring_buffer_init(&ring, storage, sizeof(storage)) ||
        (ring_buffer_write(&ring, input, 4u) != 4u) ||
        (ring_buffer_read_span(&ring).length != 4u) ||
        ring_buffer_commit_consumed(&ring, 5u) ||
        !ring_buffer_commit_consumed(&ring, 4u)) {
        return false;
    }

    if (!ring_buffer_init(&ring, storage, sizeof(storage))) {
        return false;
    }

    ring.producer = UINT32_MAX;
    ring.consumer = UINT32_MAX;
    ring_buffer_write_byte_overwrite(&ring, 0x5au);
    return (ring_buffer_read(&ring, output, 1u) == 1u) && (output[0] == 0x5au);
}

size_t ring_buffer_occupancy(const ring_buffer_t *ring)
{
    if (ring == NULL) {
        return 0u;
    }

    return ring_buffer_available(ring);
}

size_t ring_buffer_free_space(const ring_buffer_t *ring)
{
    if (ring == NULL) {
        return 0u;
    }

    return (size_t)(ring->size - ring_buffer_available(ring));
}

ring_buffer_span_t ring_buffer_read_span(ring_buffer_t *ring)
{
    ring_buffer_span_t span = {0};
    uint32_t occupancy;
    uint32_t offset;
    uint32_t contiguous;

    if ((ring == NULL) || (ring->storage == NULL)) {
        return span;
    }

    occupancy = ring_buffer_available(ring);
    if (occupancy == 0u) {
        ring->consumer_reserved_count = 0u;
        return span;
    }

    offset = ring->consumer & ring->mask;
    contiguous = ring->size - offset;
    span.data = ring->storage + offset;
    span.length = (size_t)ring_buffer_min(occupancy, contiguous);
    ring->consumer_reserved_sequence = ring->consumer;
    ring->consumer_reserved_count = (uint32_t)span.length;
    return span;
}

ring_buffer_span_t ring_buffer_write_span(ring_buffer_t *ring)
{
    ring_buffer_span_t span = {0};
    uint32_t free_space;
    uint32_t offset;
    uint32_t contiguous;

    if ((ring == NULL) || (ring->storage == NULL)) {
        return span;
    }

    free_space = ring->size - ring_buffer_available(ring);
    if (free_space == 0u) {
        ring->producer_reserved_count = 0u;
        return span;
    }

    offset = ring->producer & ring->mask;
    contiguous = ring->size - offset;
    span.data = ring->storage + offset;
    span.length = (size_t)ring_buffer_min(free_space, contiguous);
    ring->producer_reserved_sequence = ring->producer;
    ring->producer_reserved_count = (uint32_t)span.length;
    return span;
}

bool ring_buffer_commit_produced(ring_buffer_t *ring, size_t count)
{
    if ((ring == NULL) ||
        (ring->producer != ring->producer_reserved_sequence) ||
        (count > ring->producer_reserved_count)) {
        return false;
    }

    __dmb();
    ring->producer += (uint32_t)count;
    ring->producer_reserved_count = 0u;
    ring_buffer_update_high_watermark(ring);
    return true;
}

bool ring_buffer_commit_consumed(ring_buffer_t *ring, size_t count)
{
    if ((ring == NULL) ||
        (ring->consumer != ring->consumer_reserved_sequence) ||
        (count > ring->consumer_reserved_count)) {
        return false;
    }

    __dmb();
    ring->consumer += (uint32_t)count;
    ring->consumer_reserved_count = 0u;
    return true;
}

void ring_buffer_produce_external(ring_buffer_t *ring, uint32_t count)
{
    if ((ring == NULL) || (count == 0u)) {
        return;
    }

    __dmb();
    ring->producer += count;
    ring->producer_reserved_count = 0u;
    ring_buffer_update_high_watermark(ring);
}

size_t ring_buffer_write(ring_buffer_t *ring, const uint8_t *data, size_t length)
{
    size_t total_written = 0u;

    if ((ring == NULL) || (data == NULL)) {
        return 0u;
    }

    while (total_written < length) {
        ring_buffer_span_t span = ring_buffer_write_span(ring);
        size_t chunk;

        if (span.length == 0u) {
            break;
        }

        chunk = (length - total_written < span.length) ? (length - total_written) : span.length;
        memcpy(span.data, data + total_written, chunk);
        if (!ring_buffer_commit_produced(ring, chunk)) {
            break;
        }
        total_written += chunk;
    }

    return total_written;
}

void ring_buffer_write_byte_overwrite(ring_buffer_t *ring, uint8_t byte)
{
    if ((ring == NULL) || (ring->storage == NULL)) {
        return;
    }

    ring->storage[ring->producer & ring->mask] = byte;
    __dmb();
    ring->producer += 1u;
    ring->producer_reserved_count = 0u;
    ring_buffer_update_high_watermark(ring);
}

size_t ring_buffer_read(ring_buffer_t *ring, uint8_t *data, size_t length)
{
    size_t total_read = 0u;

    if ((ring == NULL) || (data == NULL)) {
        return 0u;
    }

    while (total_read < length) {
        ring_buffer_span_t span = ring_buffer_read_span(ring);
        size_t chunk;

        if (span.length == 0u) {
            break;
        }

        chunk = (length - total_read < span.length) ? (length - total_read) : span.length;
        memcpy(data + total_read, span.data, chunk);
        if (!ring_buffer_commit_consumed(ring, chunk)) {
            break;
        }
        total_read += chunk;
    }

    return total_read;
}

size_t ring_buffer_high_watermark(const ring_buffer_t *ring)
{
    if (ring == NULL) {
        return 0u;
    }

    return ring->high_watermark;
}

size_t ring_buffer_overflow_count(const ring_buffer_t *ring)
{
    if (ring == NULL) {
        return 0u;
    }

    return ring->overflow_count;
}

size_t ring_buffer_pending_overflow(const ring_buffer_t *ring)
{
    if (ring == NULL) {
        return 0u;
    }

    return (size_t)ring_buffer_pending_overflow_internal(ring);
}

size_t ring_buffer_recover_overflow(ring_buffer_t *ring)
{
    uint32_t overwritten;

    if (ring == NULL) {
        return 0u;
    }

    overwritten = ring_buffer_pending_overflow_internal(ring);
    if (overwritten != 0u) {
        __dmb();
        ring->consumer += overwritten;
        ring->overflow_count += overwritten;
        ring->consumer_reserved_count = 0u;
    }

    return (size_t)overwritten;
}