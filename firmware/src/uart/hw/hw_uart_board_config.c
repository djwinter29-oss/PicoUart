// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file hw_uart_board_config.c
 * @brief Compile-time hardware-UART assignments referencing the common board table.
 */

#include "uart/hw/hw_uart_driver.h"

const hw_uart_driver_config_t hw_uart_driver_configs[UART_PORT_COUNT] = {
    [UART_PORT_0] = {.port = &uart_board_ports[UART_PORT_0], .instance = uart0, .hardware_flow_control = false},
    [UART_PORT_1] = {.port = &uart_board_ports[UART_PORT_1], .instance = uart1, .hardware_flow_control = false},
};