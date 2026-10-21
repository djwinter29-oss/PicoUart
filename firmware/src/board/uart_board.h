/**
 * @file uart_board.h
 * @brief Board-specific logical UART port mapping.
 *
 * The table declared here is defined by @ref uart_board.c and is the single
 * source of truth for logical-port backend selection, GPIO mapping, and startup line coding.
 * HW peripheral and PIO state-machine resources are owned by their respective backend drivers.
 */

#ifndef PICO_UART_BOARD_H
#define PICO_UART_BOARD_H

#include "uart/uart_driver.h"

/** @brief Startup line rate assigned to every logical UART port. */
#define PICO_UART_BOARD_DEFAULT_BAUD_RATE 115200u

/** @brief Static startup records for all logical UART ports. */
extern const uart_driver_port_info_t uart_board_ports[UART_PORT_COUNT];

#endif
