/**
 * @file bridge.h
 * @brief Backend-independent UART ring bridging helpers.
 */

#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include "uart/ring_buffer/ring_buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Check whether an RX span remains valid after copying it to a snapshot.
 * @param context Caller-owned backend context.
 * @param consumer_sequence RX ring sequence captured for the offered span.
 * @return `true` when the backend has not overwritten the span.
 */
typedef bool (*uart_bridge_rx_snapshot_is_current_t)(void *context,
                                                      uint32_t consumer_sequence);

/**
 * @brief Drain a DMA-backed RX ring through a caller-owned writer.
 * @param rx_ring UART-to-USB ring.
 * @param capacity Maximum bytes to commit, or zero for no limit.
 * @param writer Callback that accepts bytes from a private snapshot.
 * @param writer_context Opaque context passed to @p writer.
 * @param snapshot_is_current Backend check for live DMA progress.
 * @param snapshot_context Opaque context passed to @p snapshot_is_current.
 * @param stats_sequence Worker-owned sequence counter protecting backend state.
 * @return Number of RX bytes committed from @p rx_ring.
 */
size_t uart_bridge_drain_rx(ring_buffer_t *rx_ring,
                            size_t capacity,
                            uint32_t (*writer)(void *context, const uint8_t *data, uint32_t length),
                            void *writer_context,
                            uart_bridge_rx_snapshot_is_current_t snapshot_is_current,
                            void *snapshot_context,
                            volatile uint32_t *stats_sequence);

/**
 * @brief Retire unread RX bytes already overwritten by the live producer.
 * @param rx_ring UART-to-USB ring.
 * @return Number of overwritten bytes retired.
 */
size_t uart_bridge_recover_rx(ring_buffer_t *rx_ring);

/**
 * @brief Fill a TX ring through a caller-owned reader.
 * @param tx_ring USB-to-UART ring.
 * @param capacity Maximum bytes to commit, or zero for no limit.
 * @param reader Callback that fills the offered ring span.
 * @param context Opaque context passed to @p reader.
 * @return Number of TX bytes committed into @p tx_ring.
 */
size_t uart_bridge_fill_tx(ring_buffer_t *tx_ring,
                           size_t capacity,
                           uint32_t (*reader)(void *context, uint8_t *data, uint32_t length),
                           void *context);

#endif