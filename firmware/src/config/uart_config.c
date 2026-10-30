// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file uart_config.c
 * @brief Backend-neutral PicoUart logical UART configuration.
 *
 * This table owns logical-port identity, backend selection, GPIO assignments,
 * and startup line coding. Backend-specific resources remain with each driver.
 */

#include "uart/uart_driver.h"

/** @brief Default startup line rate used by this configuration table. */
#define UART_CONFIG_DEFAULT_BAUD_RATE 115200u

const uart_driver_port_info_t uart_board_ports[UART_PORT_COUNT] = {
    {
        .id = UART_PORT_0,
        .backend = UART_DRIVER_BACKEND_HW,
        .baud_rate = UART_CONFIG_DEFAULT_BAUD_RATE,
        .tx_pin = 0u,
        .rx_pin = 1u,
        .rts_pin = 3u,
        .cts_pin = 2u,
        .data_bits = 8u,
        .stop_bits = 1u,
        .parity = UART_DRIVER_PARITY_NONE,
    },
    {
        .id = UART_PORT_1,
        .backend = UART_DRIVER_BACKEND_HW,
        .baud_rate = UART_CONFIG_DEFAULT_BAUD_RATE,
        .tx_pin = 4u,
        .rx_pin = 5u,
        .rts_pin = 7u,
        .cts_pin = 6u,
        .data_bits = 8u,
        .stop_bits = 1u,
        .parity = UART_DRIVER_PARITY_NONE,
    },
    {
        .id = UART_PORT_2,
        .backend = UART_DRIVER_BACKEND_PIO,
        .baud_rate = UART_CONFIG_DEFAULT_BAUD_RATE,
        .tx_pin = 8u,
        .rx_pin = 9u,
        .rts_pin = 10u,
        .cts_pin = 11u,
        .data_bits = 8u,
        .stop_bits = 1u,
        .parity = UART_DRIVER_PARITY_NONE,
    },
    {
        .id = UART_PORT_3,
        .backend = UART_DRIVER_BACKEND_PIO,
        .baud_rate = UART_CONFIG_DEFAULT_BAUD_RATE,
        .tx_pin = 12u,
        .rx_pin = 13u,
        .rts_pin = 14u,
        .cts_pin = 15u,
        .data_bits = 8u,
        .stop_bits = 1u,
        .parity = UART_DRIVER_PARITY_NONE,
    },
    {
        .id = UART_PORT_4,
        .backend = UART_DRIVER_BACKEND_PIO,
        .baud_rate = UART_CONFIG_DEFAULT_BAUD_RATE,
        .tx_pin = 16u,
        .rx_pin = 17u,
        .rts_pin = 18u,
        .cts_pin = 19u,
        .data_bits = 8u,
        .stop_bits = 1u,
        .parity = UART_DRIVER_PARITY_NONE,
    },
    {
        .id = UART_PORT_5,
        .backend = UART_DRIVER_BACKEND_PIO,
        .baud_rate = UART_CONFIG_DEFAULT_BAUD_RATE,
        .tx_pin = 20u,
        .rx_pin = 21u,
        .rts_pin = 22u,
        .cts_pin = 26u,
        .data_bits = 8u,
        .stop_bits = 1u,
        .parity = UART_DRIVER_PARITY_NONE,
    },
};