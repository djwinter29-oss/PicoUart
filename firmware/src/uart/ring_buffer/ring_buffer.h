/**
 * @file ring_buffer.h
 * @brief Fixed-size ring-buffer helpers for PicoUart DMA and USB bridging.
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief One contiguous readable or writable window inside a ring buffer.
 */
typedef struct {
    uint8_t *data; /**< Pointer to the contiguous window start. */
    size_t length; /**< Number of bytes available in the window. */
} ring_buffer_span_t;

/**
 * @brief Minimal ring-buffer state for the planned DMA-friendly design.
 */
typedef struct {
    uint8_t *storage; /**< Caller-owned buffer storage. */
    size_t size; /**< Total ring size in bytes. */
    size_t mask; /**< Power-of-two mask used for wrap handling. */
    volatile size_t producer; /**< Producer index. */
    volatile size_t consumer; /**< Consumer index. */
    volatile size_t high_watermark; /**< Largest observed occupancy. */
    volatile size_t overflow_count; /**< Number of rejected bytes due to a full ring. */
} ring_buffer_t;

/**
 * @brief Return whether a proposed ring size is valid.
 * @param size Ring size in bytes.
 * @return `true` when @p size is non-zero and a power of two.
 */
bool ring_buffer_size_is_valid(size_t size);

/**
 * @brief Initialize a ring buffer with caller-owned storage.
 * @param ring Ring state to initialize.
 * @param storage Caller-owned storage area.
 * @param size Storage size in bytes. Must be a power of two.
 * @return `true` on success, otherwise `false`.
 */
bool ring_buffer_init(ring_buffer_t *ring, uint8_t *storage, size_t size);

/**
 * @brief Reset a ring buffer to the empty state.
 * @param ring Ring state to reset.
 */
void ring_buffer_reset(ring_buffer_t *ring);

/**
 * @brief Return the number of bytes currently stored in the ring.
 * @param ring Ring to inspect.
 * @return Number of readable bytes.
 */
size_t ring_buffer_occupancy(const ring_buffer_t *ring);

/**
 * @brief Return the free space currently available in the ring.
 * @param ring Ring to inspect.
 * @return Number of writable bytes.
 */
size_t ring_buffer_free_space(const ring_buffer_t *ring);

/**
 * @brief Return the next contiguous readable span in the ring.
 * @param ring Ring to inspect.
 * @return Readable contiguous span.
 */
ring_buffer_span_t ring_buffer_read_span(const ring_buffer_t *ring);

/**
 * @brief Return the next contiguous writable span in the ring.
 * @param ring Ring to inspect.
 * @return Writable contiguous span.
 */
ring_buffer_span_t ring_buffer_write_span(ring_buffer_t *ring);

/**
 * @brief Commit bytes produced into the ring.
 * @param ring Ring to update.
 * @param count Number of bytes produced.
 * @return `true` when the commit succeeded, otherwise `false`.
 */
bool ring_buffer_commit_produced(ring_buffer_t *ring, size_t count);

/**
 * @brief Commit bytes consumed from the ring.
 * @param ring Ring to update.
 * @param count Number of bytes consumed.
 * @return `true` when the commit succeeded, otherwise `false`.
 */
bool ring_buffer_commit_consumed(ring_buffer_t *ring, size_t count);

/**
 * @brief Publish an externally-produced absolute producer index into the ring.
 * @param ring Ring to update.
 * @param producer Absolute producer index.
 * @param preserve_newest When `true`, drop the oldest unread bytes on overflow.
 */
void ring_buffer_publish_producer(ring_buffer_t *ring, size_t producer, bool preserve_newest);

/**
 * @brief Copy bytes into the ring from a caller buffer.
 * @param ring Ring to write.
 * @param data Source bytes.
 * @param length Requested byte count.
 * @return Number of bytes written.
 */
size_t ring_buffer_write(ring_buffer_t *ring, const uint8_t *data, size_t length);

/**
 * @brief Write one byte into the ring, dropping the oldest unread byte on overflow.
 * @param ring Ring to update.
 * @param byte Byte to publish.
 */
void ring_buffer_write_byte_preserve_newest(ring_buffer_t *ring, uint8_t byte);

/**
 * @brief Copy bytes out of the ring into a caller buffer.
 * @param ring Ring to read.
 * @param data Destination buffer.
 * @param length Requested byte count.
 * @return Number of bytes copied.
 */
size_t ring_buffer_read(ring_buffer_t *ring, uint8_t *data, size_t length);

/**
 * @brief Return the largest observed occupancy for the ring.
 * @param ring Ring to inspect.
 * @return High-water mark in bytes.
 */
size_t ring_buffer_high_watermark(const ring_buffer_t *ring);

/**
 * @brief Return the total overflow count for the ring.
 * @param ring Ring to inspect.
 * @return Overflow count in bytes.
 */
size_t ring_buffer_overflow_count(const ring_buffer_t *ring);

#endif