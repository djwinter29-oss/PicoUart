/**
 * @file uart_board.h
 * @brief Board-specific logical UART port mapping.
 */

#ifndef PICO_UART_BOARD_H
#define PICO_UART_BOARD_H

#include "uart/hw/driver.h"
#include "uart/pio/driver.h"
#include "uart/uart_driver.h"

/** @brief Startup line rate assigned to every logical UART port. */
#define PICO_UART_BOARD_DEFAULT_BAUD_RATE 115200u

/**
 * @brief Immutable board assignment for one logical UART port.
 */
typedef struct {
    uart_driver_port_info_t info; /**< Public port identity and GPIO mapping. */
    union {
        hw_uart_driver_config_t hw; /**< Hardware UART peripheral assignment. */
        pio_uart_driver_config_t pio; /**< PIO UART peripheral assignment. */
    } backend; /**< Backend-specific board configuration. */
} uart_board_port_config_t;

/** @brief Board assignments for all logical UART ports. */
extern const uart_board_port_config_t uart_board_ports[UART_PORT_COUNT];

#endif
