/**
 * @file uart_board.c
 * @brief PicoUart board-specific UART pin and peripheral mapping.
 */

#include "config/uart_board.h"

const uart_board_port_config_t uart_board_ports[UART_PORT_COUNT] = {
    {
        .info = {UART_PORT_0, UART_DRIVER_BACKEND_HW, PICO_UART_BOARD_DEFAULT_BAUD_RATE, 0u, 1u},
        .backend.hw = {
            uart0,
            PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            0u,
            1u,
            2u,
            3u,
            false,
            8u,
            1u,
            UART_PARITY_NONE,
        },
    },
    {
        .info = {UART_PORT_1, UART_DRIVER_BACKEND_HW, PICO_UART_BOARD_DEFAULT_BAUD_RATE, 4u, 5u},
        .backend.hw = {
            uart1,
            PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            4u,
            5u,
            6u,
            7u,
            false,
            8u,
            1u,
            UART_PARITY_NONE,
        },
    },
    {
        .info = {UART_PORT_2, UART_DRIVER_BACKEND_PIO, PICO_UART_BOARD_DEFAULT_BAUD_RATE, 8u, 9u},
        .backend.pio = {
            pio0,
            0u,
            1u,
            PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            8u,
            9u,
            PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
            PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
        },
    },
    {
        .info = {UART_PORT_3, UART_DRIVER_BACKEND_PIO, PICO_UART_BOARD_DEFAULT_BAUD_RATE, 12u, 13u},
        .backend.pio = {
            pio0,
            2u,
            3u,
            PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            12u,
            13u,
            PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
            PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
        },
    },
    {
        .info = {UART_PORT_4, UART_DRIVER_BACKEND_PIO, PICO_UART_BOARD_DEFAULT_BAUD_RATE, 16u, 17u},
        .backend.pio = {
            pio1,
            0u,
            1u,
            PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            16u,
            17u,
            PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
            PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
        },
    },
    {
        .info = {UART_PORT_5, UART_DRIVER_BACKEND_PIO, PICO_UART_BOARD_DEFAULT_BAUD_RATE, 20u, 21u},
        .backend.pio = {
            pio1,
            2u,
            3u,
            PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            20u,
            21u,
            PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
            PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
        },
    },
};
