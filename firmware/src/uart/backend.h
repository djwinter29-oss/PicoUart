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
    uint32_t controller_tx_bytes; /**< Bytes transmitted by the backend. */
    uint32_t controller_rx_bytes; /**< Bytes received by the backend. */
    uint32_t rx_error_count; /**< Receive-status errors observed by the backend. */
} uart_backend_stats_t;

/**
 * @brief Operations shared by hardware-UART and PIO-UART backend storage.
 *
 * The @p instance pointer always addresses the active member of the logical
 * port's tagged backend union. It is private to the UART implementation.
 */
typedef struct {
    bool (*is_initialized)(const void *instance); /**< Return backend ready state. */
    bool (*init)(void *instance); /**< Initialize one backend. */
    void (*deinit)(void *instance); /**< Deinitialize one backend. */
    void (*poll)(void *instance, bool tx_launch_allowed); /**< Advance worker-owned I/O. */
    ring_buffer_t *(*rx_ring)(void *instance); /**< Return the UART-to-USB ring. */
    ring_buffer_t *(*tx_ring)(void *instance); /**< Return the USB-to-UART ring. */
    bool (*line_coding_matches)(const void *instance,
                                const uart_driver_line_coding_t *line_coding); /**< Compare active line coding. */
    bool (*line_coding_acceptable)(const uart_driver_line_coding_t *line_coding); /**< Check permanent support. */
    bool (*set_line_coding)(void *instance,
                            const uart_driver_line_coding_t *line_coding); /**< Apply a safe line-coding change. */
    bool (*rx_snapshot_is_current)(const void *instance,
                                   uint32_t consumer_sequence); /**< Validate a copied RX span. */
    void (*clear_rx_error_baseline)(void *instance); /**< Discard startup RX errors. */
    uint32_t (*baud_rate)(const void *instance); /**< Return active baud rate. */
    uart_backend_stats_t (*stats)(const void *instance); /**< Return backend counters. */
} uart_backend_ops_t;

/**
 * @brief Return the operations table for a configured backend kind.
 * @param backend Backend class from the board mapping.
 * @return Backend operations, or `NULL` for an unknown class.
 */
const uart_backend_ops_t *uart_backend_ops_for_type(uart_driver_backend_t backend);

/** @brief Enable all UART-backend RX DMA IRQ handlers on the worker core. */
void uart_backend_enable_rx_dma_irq(void);

#endif