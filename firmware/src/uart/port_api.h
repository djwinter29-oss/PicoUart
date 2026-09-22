/**
 * @file port_api.h
 * @brief Private public-port facade operations for the UART driver.
 */

#ifndef UART_PORT_API_H
#define UART_PORT_API_H

#include "hardware/sync.h"
#include "uart/runtime.h"

/**
 * @brief State references required by public logical-port operations.
 */
typedef struct {
    uart_runtime_port_t *ports; /**< Backend storage for all logical ports. */
    volatile uint32_t *stats_sequence; /**< Worker sequence counters for stats/RX snapshots. */
    volatile uint8_t *status_flags; /**< Control-pending status flags. */
    spin_lock_t *status_lock; /**< Lock protecting status flags and metadata. */
} uart_port_api_t;

size_t uart_port_api_count(void);
bool uart_port_api_is_ready(const uart_port_api_t *api, uart_port_id_t port_id);
size_t uart_port_api_drain_rx(const uart_port_api_t *api,
                              uart_port_id_t port_id,
                              size_t capacity,
                              uint32_t (*writer)(void *context,
                                                 const uint8_t *data,
                                                 uint32_t length),
                              void *context);
size_t uart_port_api_recover_rx(const uart_port_api_t *api, uart_port_id_t port_id);
size_t uart_port_api_fill_tx(const uart_port_api_t *api,
                             uart_port_id_t port_id,
                             size_t capacity,
                             uint32_t (*reader)(void *context,
                                                uint8_t *data,
                                                uint32_t length),
                             void *context);
bool uart_port_api_info(const uart_port_api_t *api,
                        uart_port_id_t port_id,
                        uart_driver_port_info_t *info);
bool uart_port_api_stats(const uart_port_api_t *api,
                         uart_port_id_t port_id,
                         uart_driver_port_stats_t *stats);

#endif