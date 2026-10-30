// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file backend.h
 * @brief Private common operations contract for UART backend instances.
 */

#ifndef UART_BACKEND_H
#define UART_BACKEND_H

#include "uart/ring_buffer/ring_buffer.h"
#include "uart/uart_driver.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Transport counters reported by one UART backend.
 */
typedef struct {
    uint32_t uart_tx_bytes; /**< Bytes handed to the UART peripheral/FIFO, not wire completion. */
    uint32_t uart_rx_bytes; /**< DMA-received bytes published into the RX ring. */
    uint32_t rx_error_count;      /**< Receive-status errors observed by the backend. */
} uart_backend_stats_t;

/**
 * @brief Operations shared by hardware-UART and PIO-UART backend storage.
 *
 * Each context points to the selected driver's runtime instance, owned by the
 * logical port. Drivers implement this signature directly without forwarding wrappers.
 * Null queries return false, NULL, zero baud, or zero counters as appropriate.
 * Matching line coding requires an initialized instance. A valid, clock-feasible
 * setter stages settings on a bound inactive instance or applies them safely to an active one.
 * Configuration binding accepts only an inactive instance
 * and its immutable board-table entry; a failed binding leaves state unchanged.
 * Non-null contexts must have the selected driver's concrete type. Ring/rate/counter
 * queries may inspect inactive instances; use is_initialized before treating them as live.
 */
typedef struct {
    bool (*load_config)(void *instance,
                        const uart_driver_port_info_t *port_info);                /**< Resolve backend-owned resources. */
    bool (*is_initialized)(const void *instance);         /**< Return backend ready state. */
    bool (*init)(void *instance);                         /**< Initialize one backend. */
    void (*deinit)(void *instance);                       /**< Deinitialize one backend. */
    void (*poll)(void *instance, bool tx_launch_allowed); /**< Advance worker-owned I/O. */
    ring_buffer_t *(*rx_ring)(void *instance);            /**< Return the UART-to-USB ring. */
    ring_buffer_t *(*tx_ring)(void *instance);            /**< Return the USB-to-UART ring. */
    bool (*line_coding_matches)(const void *instance,
                                const uart_driver_line_coding_t *line_coding);    /**< Compare active line coding. */
    bool (*line_coding_acceptable)(const uart_driver_line_coding_t *line_coding); /**< Check permanent support. */
    bool (*set_line_coding)(void *instance,
                            const uart_driver_line_coding_t *line_coding); /**< Stage stopped settings or safely apply. */
    bool (*restart)(void *instance);              /**< Restart one channel and discard its queued bytes. */
    bool (*rx_snapshot_is_current)(const void *instance,
                                   uint32_t consumer_sequence);             /**< Validate a copied RX span. */
    void (*clear_rx_error_baseline)(void *instance);     /**< Discard startup RX errors. */
    uint32_t (*baud_rate)(const void *instance);         /**< Return active baud rate. */
    uart_backend_stats_t (*stats)(const void *instance); /**< Return backend counters. */
} uart_backend_ops_t;

/**
 * @brief Return whether a backend operation table is complete.
 * @param ops Operation table to validate.
 * @return `true` when every backend operation is present.
 */
static inline bool uart_backend_ops_is_complete(const uart_backend_ops_t *ops)
{
    return (ops != NULL) && (ops->load_config != NULL) && (ops->is_initialized != NULL) &&
           (ops->init != NULL) && (ops->deinit != NULL) &&
           (ops->poll != NULL) && (ops->rx_ring != NULL) && (ops->tx_ring != NULL) &&
           (ops->line_coding_matches != NULL) && (ops->line_coding_acceptable != NULL) &&
           (ops->set_line_coding != NULL) && (ops->restart != NULL) &&
           (ops->rx_snapshot_is_current != NULL) &&
           (ops->clear_rx_error_baseline != NULL) && (ops->baud_rate != NULL) && (ops->stats != NULL);
}

/** @brief Hardware UART operations implemented directly by the HW driver. */
extern const uart_backend_ops_t uart_backend_hw_ops;
/** @brief PIO UART operations implemented directly by the PIO driver. */
extern const uart_backend_ops_t uart_backend_pio_ops;

/**
 * @brief Select a complete driver-owned operations table.
 * @param backend Backend class from the board mapping.
 * @return Operations, or `NULL` for an unknown or incomplete backend.
 */
static inline const uart_backend_ops_t *uart_backend_ops_for_type(uart_driver_backend_t backend)
{
    const uart_backend_ops_t *ops = backend == UART_DRIVER_BACKEND_HW ? &uart_backend_hw_ops
                                  : backend == UART_DRIVER_BACKEND_PIO ? &uart_backend_pio_ops : NULL;
    return uart_backend_ops_is_complete(ops) ? ops : NULL;
}

#endif