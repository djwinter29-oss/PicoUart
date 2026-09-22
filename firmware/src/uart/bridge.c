/**
 * @file bridge.c
 * @brief Backend-independent UART ring bridging helpers.
 */

#include "uart/bridge.h"

#include "hardware/sync.h"

#include <string.h>

/** @brief Maximum RX snapshot copied before handing bytes to a USB writer. */
#define UART_BRIDGE_RX_SNAPSHOT_SIZE 1024u

size_t uart_bridge_drain_rx(ring_buffer_t *rx_ring,
                            size_t capacity,
                            uint32_t (*writer)(void *context, const uint8_t *data, uint32_t length),
                            void *writer_context,
                            uart_bridge_rx_snapshot_is_current_t snapshot_is_current,
                            void *snapshot_context,
                            volatile uint32_t *stats_sequence)
{
    size_t total_written = 0u;
    static uint8_t snapshot[UART_BRIDGE_RX_SNAPSHOT_SIZE];

    if ((rx_ring == NULL) || (writer == NULL) || (snapshot_is_current == NULL) ||
        (stats_sequence == NULL)) {
        return 0u;
    }

    (void)ring_buffer_recover_overflow(rx_ring);
    while ((capacity == 0u) || (total_written < capacity)) {
        ring_buffer_span_t span = ring_buffer_read_span(rx_ring);
        uint32_t offered;
        uint32_t written;
        uint32_t consumer_sequence;
        uint32_t initial_stats_sequence;

        if (span.length == 0u) {
            break;
        }

        offered = (uint32_t)span.length;
        if ((capacity != 0u) && (offered > (capacity - total_written))) {
            offered = (uint32_t)(capacity - total_written);
        }
        if (offered > sizeof(snapshot)) {
            offered = sizeof(snapshot);
        }

        consumer_sequence = rx_ring->consumer_reserved_sequence;
        initial_stats_sequence = *stats_sequence;
        if ((initial_stats_sequence & 1u) != 0u) {
            return total_written;
        }

        memcpy(snapshot, span.data, offered);
        __dmb();
        if (!ring_buffer_read_span_is_current(rx_ring) ||
            !snapshot_is_current(snapshot_context, consumer_sequence) ||
            (*stats_sequence != initial_stats_sequence)) {
            (void)ring_buffer_recover_overflow(rx_ring);
            return total_written;
        }

        written = writer(writer_context, snapshot, offered);
        if (written == 0u) {
            break;
        }
        if ((written > offered) || !ring_buffer_commit_snapshot_consumed(rx_ring, written)) {
            return total_written;
        }

        total_written += written;
        if (written < offered) {
            break;
        }
    }

    return total_written;
}

size_t uart_bridge_recover_rx(ring_buffer_t *rx_ring)
{
    return ring_buffer_recover_overflow(rx_ring);
}

size_t uart_bridge_fill_tx(ring_buffer_t *tx_ring,
                           size_t capacity,
                           uint32_t (*reader)(void *context, uint8_t *data, uint32_t length),
                           void *context)
{
    size_t total_read = 0u;

    if ((tx_ring == NULL) || (reader == NULL)) {
        return 0u;
    }

    while ((capacity == 0u) || (total_read < capacity)) {
        ring_buffer_span_t span = ring_buffer_write_span(tx_ring);
        uint32_t offered;
        uint32_t read;

        if (span.length == 0u) {
            break;
        }

        offered = (uint32_t)span.length;
        if ((capacity != 0u) && (offered > (capacity - total_read))) {
            offered = (uint32_t)(capacity - total_read);
        }

        read = reader(context, span.data, offered);
        if (read == 0u) {
            break;
        }
        if ((read > offered) || !ring_buffer_commit_produced(tx_ring, read)) {
            return total_read;
        }

        total_read += read;
        if (read < offered) {
            break;
        }
    }

    return total_read;
}