/**
 * @file topology.h
 * @brief Host-testable UART board-topology validation.
 */

#ifndef UART_TOPOLOGY_H
#define UART_TOPOLOGY_H

#include "uart/uart_driver.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Invalid GPIO marker in a normalized topology port. */
#define UART_TOPOLOGY_PIN_UNASSIGNED UINT32_MAX

/**
 * @brief Hardware-independent resource assignment for one logical UART port.
 */
typedef struct {
    uart_port_id_t id; /**< Logical port identity. */
    uart_driver_backend_t backend; /**< Backend class. */
    uint32_t baud_rate; /**< Public baud rate. */
    uint32_t tx_pin; /**< Public TX GPIO. */
    uint32_t rx_pin; /**< Public RX GPIO. */
    uintptr_t backend_instance; /**< Hardware UART or PIO block identity. */
    uint32_t backend_baud_rate; /**< Backend-configured baud rate. */
    uint32_t backend_tx_pin; /**< Backend-configured TX GPIO. */
    uint32_t backend_rx_pin; /**< Backend-configured RX GPIO. */
    uint32_t tx_state_machine; /**< PIO TX state machine; ignored for hardware UART. */
    uint32_t rx_state_machine; /**< PIO RX state machine; ignored for hardware UART. */
    uint32_t target_gpio_count; /**< Number of valid GPIO indices on the selected target. */
    uint32_t rts_pin; /**< Backend RTS GPIO; ignored unless @ref rts_enabled. */
    uint32_t cts_pin; /**< Backend CTS GPIO; ignored unless @ref cts_enabled. */
    bool rts_enabled; /**< True when RTS is an active topology resource. */
    bool cts_enabled; /**< True when CTS is an active topology resource. */
} uart_topology_port_t;

/**
 * @brief Validate a fixed 2-hardware-UART / 4-PIO-UART port assignment.
 * @param ports Normalized port map.
 * @param port_count Number of entries in @p ports.
 * @return `true` when every pin and backend resource is uniquely and consistently assigned.
 */
bool uart_topology_validate(const uart_topology_port_t *ports, size_t port_count);

#endif
