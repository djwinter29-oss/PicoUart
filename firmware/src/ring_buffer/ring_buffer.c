/**
 * @file ring_buffer.c
 * @brief Fixed-size ring-buffer helpers for PicoUart DMA and USB bridging.
 */

#include "ring_buffer/ring_buffer.h"

#include <string.h>

static size_t ring_buffer_min_size(size_t left, size_t right)
{
    return (left < right) ? left : right;
}

bool ring_buffer_size_is_valid(size_t size)
{
    return (size != 0u) && ((size & (size - 1u)) == 0u);
}

bool ring_buffer_init(ring_buffer_t *ring, uint8_t *storage, size_t size)
{
    if ((ring == NULL) || (storage == NULL) || !ring_buffer_size_is_valid(size)) {
        return false;
    }

    ring->storage = storage;
    ring->size = size;
    ring->mask = size - 1u;
    ring_buffer_reset(ring);
    return true;
}

void ring_buffer_reset(ring_buffer_t *ring)
{
    if (ring == NULL) {
        return;
    }

    ring->producer = 0u;
    ring->consumer = 0u;
    ring->high_watermark = 0u;
    ring->overflow_count = 0u;
}

size_t ring_buffer_occupancy(const ring_buffer_t *ring)
{
    if (ring == NULL) {
        return 0u;
    }

    return ring->producer - ring->consumer;
}

size_t ring_buffer_free_space(const ring_buffer_t *ring)
{
    if (ring == NULL) {
        return 0u;
    }

    return ring->size - ring_buffer_occupancy(ring);
}

ring_buffer_span_t ring_buffer_read_span(const ring_buffer_t *ring)
{
    ring_buffer_span_t span = {0};
    size_t occupancy;
    size_t offset;
    size_t contiguous;

    if ((ring == NULL) || (ring->storage == NULL)) {
        return span;
    }

    occupancy = ring_buffer_occupancy(ring);
    if (occupancy == 0u) {
        return span;
    }

    offset = ring->consumer & ring->mask;
    contiguous = ring->size - offset;
    span.data = ring->storage + offset;
    span.length = ring_buffer_min_size(occupancy, contiguous);
    return span;
}

ring_buffer_span_t ring_buffer_write_span(ring_buffer_t *ring)
{
    ring_buffer_span_t span = {0};
    size_t free_space;
    size_t offset;
    size_t contiguous;

    if ((ring == NULL) || (ring->storage == NULL)) {
        return span;
    }

    free_space = ring_buffer_free_space(ring);
    if (free_space == 0u) {
        return span;
    }

    offset = ring->producer & ring->mask;
    contiguous = ring->size - offset;
    span.data = ring->storage + offset;
    span.length = ring_buffer_min_size(free_space, contiguous);
    return span;
}

bool ring_buffer_commit_produced(ring_buffer_t *ring, size_t count)
{
    size_t occupancy;

    if ((ring == NULL) || (count > ring_buffer_free_space(ring))) {
        return false;
    }

    ring->producer += count;
    occupancy = ring_buffer_occupancy(ring);
    if (occupancy > ring->high_watermark) {
        ring->high_watermark = occupancy;
    }

    return true;
}

bool ring_buffer_commit_consumed(ring_buffer_t *ring, size_t count)
{
    if ((ring == NULL) || (count > ring_buffer_occupancy(ring))) {
        return false;
    }

    ring->consumer += count;
    return true;
}

void ring_buffer_publish_producer(ring_buffer_t *ring, size_t producer, bool preserve_newest)
{
    size_t delta;
    size_t free_space;
    size_t overflow;

    if ((ring == NULL) || (producer <= ring->producer)) {
        return;
    }

    delta = producer - ring->producer;
    free_space = ring_buffer_free_space(ring);
    if (delta <= free_space) {
        (void)ring_buffer_commit_produced(ring, delta);
        return;
    }

    overflow = delta - free_space;
    ring->overflow_count += overflow;

    if (preserve_newest) {
        ring->consumer += overflow;
        ring->producer = producer;
        ring->high_watermark = ring->size;
        return;
    }

    ring->producer += free_space;
    ring->high_watermark = ring->size;
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

        chunk = ring_buffer_min_size(length - total_written, span.length);
        memcpy(span.data, data + total_written, chunk);
        if (!ring_buffer_commit_produced(ring, chunk)) {
            break;
        }

        total_written += chunk;
    }

    if (total_written < length) {
        ring->overflow_count += (length - total_written);
    }

    return total_written;
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

        chunk = ring_buffer_min_size(length - total_read, span.length);
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