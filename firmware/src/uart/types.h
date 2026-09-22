/**
 * @file types.h
 * @brief Backend-neutral UART types shared across private modules.
 */

#ifndef UART_TYPES_H
#define UART_TYPES_H

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
    UART_PORT_0 = 0, /**< Logical port 0. */
    UART_PORT_1, /**< Logical port 1. */
    UART_PORT_2, /**< Logical port 2. */
    UART_PORT_3, /**< Logical port 3. */
    UART_PORT_4, /**< Logical port 4. */
    UART_PORT_5, /**< Logical port 5. */
    UART_PORT_COUNT /**< Total number of logical UART ports. */
} uart_port_id_t;

/**
 * @brief Public view of one logical UART port.
 */
typedef struct {
    uart_port_id_t id; /**< Logical port identifier. */
    uart_driver_backend_t backend; /**< Backend class assigned to the port. */
    uint32_t baud_rate; /**< Current baud rate. */
    uint32_t tx_pin; /**< Configured TX GPIO, or unassigned marker. */
    uint32_t rx_pin; /**< Configured RX GPIO, or unassigned marker. */
} uart_driver_port_info_t;

/**
 * @brief Host-requested UART parity mode.
 */
typedef enum {
    UART_DRIVER_PARITY_NONE = 0, /**< No parity bit. */
    UART_DRIVER_PARITY_ODD, /**< Odd parity. */
    UART_DRIVER_PARITY_EVEN, /**< Even parity. */
} uart_driver_parity_t;

/**
 * @brief Worker-applied UART line-coding request.
 */
typedef struct {
    uint32_t baud_rate; /**< Requested baud rate. */
    uint8_t data_bits; /**< Requested UART data-bit count. */
    uint8_t stop_bits; /**< Requested UART stop-bit count. */
    uart_driver_parity_t parity; /**< Requested parity mode. */
} uart_driver_line_coding_t;

#endif
