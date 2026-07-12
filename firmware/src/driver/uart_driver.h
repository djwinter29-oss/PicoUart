/**
 * @file uart_driver.h
 * @brief Logical UART port table for the PicoUart firmware.
 */

#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Backend class used by a logical UART port.
 */
typedef enum {
    UART_DRIVER_BACKEND_HW = 0, /**< Hardware UART peripheral backend. */
    UART_DRIVER_BACKEND_PIO, /**< PIO UART backend. */
} uart_driver_backend_t;

/**
 * @brief Logical UART port identifiers.
 */
typedef enum {
    UART_PORT_0 = 0, /**< Logical UART port 0. */
    UART_PORT_1, /**< Logical UART port 1. */
    UART_PORT_2, /**< Logical UART port 2. */
    UART_PORT_3, /**< Logical UART port 3. */
    UART_PORT_4, /**< Logical UART port 4. */
    UART_PORT_5, /**< Logical UART port 5. */
    UART_PORT_COUNT /**< Total number of logical UART ports. */
} uart_port_id_t;

/**
 * @brief Public view of one logical UART port.
 */
typedef struct {
    uart_port_id_t id; /**< Logical port identifier. */
    uart_driver_backend_t backend; /**< Backend class assigned to the port. */
    uint32_t baud_rate; /**< Configured startup baud rate. */
    uint32_t tx_pin; /**< Configured TX GPIO, or unassigned marker. */
    uint32_t rx_pin; /**< Configured RX GPIO, or unassigned marker. */
} uart_driver_port_info_t;

/**
 * @brief Return the number of logical UART ports in the firmware.
 * @return Always 6 for the current design.
 */
size_t uart_driver_port_count(void);

/**
 * @brief Return public metadata for one logical UART port.
 * @param port_id Logical port identifier.
 * @return Pointer to immutable port metadata, or `NULL` when the index is invalid.
 */
const uart_driver_port_info_t *uart_driver_port_info(uart_port_id_t port_id);

/**
 * @brief Check that the logical UART table still matches the 2-HW and 4-PIO design.
 * @return `true` when the table is valid, otherwise `false`.
 */
bool uart_driver_validate_topology(void);

#endif