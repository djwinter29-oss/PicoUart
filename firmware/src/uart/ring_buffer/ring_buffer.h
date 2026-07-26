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
 *
 * @note Shared cursors rely on aligned 32-bit accesses and `__dmb()` barriers
 * provided by the Pico SDK target. This is not a portable C11 thread-safety
 * primitive; use this type only with its documented single-producer,
 * single-consumer ownership contract.
 */
typedef struct {
    uint8_t *storage; /**< Caller-owned buffer storage. */
    uint32_t size; /**< Total ring size in bytes. */
    uint32_t mask; /**< Power-of-two mask used for wrap handling. */
    volatile uint32_t producer; /**< Monotonic sequence written only by the producer. */
    volatile uint32_t consumer; /**< Monotonic sequence written only by the consumer. */
    volatile uint32_t high_watermark; /**< Largest observed occupancy. */
    volatile uint32_t overflow_count; /**< Bytes discarded by consumer-side overrun recovery. */
    uint32_t producer_reserved_sequence; /**< Producer sequence captured for the current writable span. */
    uint32_t producer_reserved_count; /**< Bytes available in the current writable span. */
    uint32_t consumer_reserved_sequence; /**< Consumer sequence captured for the current readable span. */
    uint32_t consumer_reserved_count; /**< Bytes available in the current readable span. */
} ring_buffer_t;

/**
 * @brief Initialize a ring buffer with caller-owned storage.
 * @param ring Ring state to initialize.
 * @param storage Caller-owned storage area.
 * @param size Storage size in bytes. Must be a power of two.
 * @return `true` on success, otherwise `false`.
 */
bool ring_buffer_init(ring_buffer_t *ring, uint8_t *storage, size_t size);

/**
 * @brief Run the fixed-size ring overwrite and sequence-wrap self-check.
 * @return `true` when the ring preserves its ownership and ordering invariants.
 */
bool ring_buffer_self_check(void);

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
 *
 * Only one consumer span may be outstanding. Requesting another span replaces
 * the previous reservation and causes its later commit to fail.
 */
ring_buffer_span_t ring_buffer_read_span(ring_buffer_t *ring);

/**
 * @brief Return the next contiguous writable span in the ring.
 * @param ring Ring to inspect.
 * @return Writable contiguous span.
 *
 * Only one producer span may be outstanding. Requesting another span replaces
 * the previous reservation and causes its later commit to fail.
 */
ring_buffer_span_t ring_buffer_write_span(ring_buffer_t *ring);

/**
 * @brief Commit bytes produced into the ring.
 * @param ring Ring to update.
 * @param count Number of bytes produced.
 * @return `true` when @p count fits the producer's most recently returned span.
 */
bool ring_buffer_commit_produced(ring_buffer_t *ring, size_t count);

/**
 * @brief Commit bytes consumed from the ring.
 * @param ring Ring to update.
 * @param count Number of bytes consumed.
 * @return `true` when @p count fits the consumer's most recently returned span.
 */
bool ring_buffer_commit_consumed(ring_buffer_t *ring, size_t count);

/**
 * @brief Advance an externally-owned producer by a byte count.
 * @param ring Ring to update.
 * @param count Number of newly produced bytes.
 *
 * The producer never advances @ref ring_buffer_t.consumer. The consumer detects
 * and records overwrites before returning a readable span.
 */
void ring_buffer_produce_external(ring_buffer_t *ring, uint32_t count);

/**
 * @brief Return the current producer index into the ring storage.
 * @param ring Ring to inspect.
 * @return `producer & mask`, suitable as a DMA write offset into @p ring storage.
 */
uint32_t ring_buffer_producer_index(const ring_buffer_t *ring);

/**
 * @brief Copy bytes into the ring from a caller buffer.
 * @param ring Ring to write.
 * @param data Source bytes.
 * @param length Requested byte count.
 * @return Number of bytes written.
 */
size_t ring_buffer_write(ring_buffer_t *ring, const uint8_t *data, size_t length);

/**
 * @brief Write one byte into the ring, overwriting the oldest unread byte when full.
 * @param ring Ring to update.
 * @param byte Byte to publish.
 *
 * The consumer records overwritten bytes when it next reads the ring.
 */
void ring_buffer_write_byte_overwrite(ring_buffer_t *ring, uint8_t byte);

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

/**
 * @brief Return unread RX bytes that have already been overwritten.
 * @param ring Ring to inspect.
 * @return Bytes the consumer must discard before it can read valid data. This
 * is a transient diagnostic; regular consumer recovery normally clears it.
 */
size_t ring_buffer_pending_overflow(const ring_buffer_t *ring);

/**
 * @brief Discard overwritten bytes before the consumer reads the ring.
 * @param ring Ring to update from its consumer context.
 * @return Number of bytes discarded during recovery. Call this regularly from
 * the consumer context to keep the 32-bit sequence distance bounded.
 */
size_t ring_buffer_recover_overflow(ring_buffer_t *ring);

#endif