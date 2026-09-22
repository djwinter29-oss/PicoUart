/**
 * @file uart_board.c
 * @brief PicoUart board-specific UART pin and peripheral mapping.
 */

#include "board/uart_board.h"

const uart_board_port_config_t uart_board_ports[UART_PORT_COUNT] = {
    {
        .info = {
            .id = UART_PORT_0,
            .backend = UART_DRIVER_BACKEND_HW,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 0u,
            .rx_pin = 1u,
        },
        .backend.hw = {
            .instance = uart0,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 0u,
            .rx_pin = 1u,
            .cts_pin = 2u,
            .rts_pin = 3u,
            .hardware_flow_control = false,
            .data_bits = 8u,
            .stop_bits = 1u,
            .parity = UART_PARITY_NONE,
        },
    },
    {
        .info = {
            .id = UART_PORT_1,
            .backend = UART_DRIVER_BACKEND_HW,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 4u,
            .rx_pin = 5u,
        },
        .backend.hw = {
            .instance = uart1,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 4u,
            .rx_pin = 5u,
            .cts_pin = 6u,
            .rts_pin = 7u,
            .hardware_flow_control = false,
            .data_bits = 8u,
            .stop_bits = 1u,
            .parity = UART_PARITY_NONE,
        },
    },
    {
        .info = {
            .id = UART_PORT_2,
            .backend = UART_DRIVER_BACKEND_PIO,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 8u,
            .rx_pin = 9u,
        },
        .backend.pio = {
            .pio = pio0,
            .tx_state_machine = 0u,
            .rx_state_machine = 1u,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 8u,
            .rx_pin = 9u,
            .rts_pin = 10u,
            .cts_pin = 11u,
            .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP |
                         PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
            .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
        },
    },
    {
        .info = {
            .id = UART_PORT_3,
            .backend = UART_DRIVER_BACKEND_PIO,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 12u,
            .rx_pin = 13u,
        },
        .backend.pio = {
            .pio = pio0,
            .tx_state_machine = 2u,
            .rx_state_machine = 3u,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 12u,
            .rx_pin = 13u,
            .rts_pin = 14u,
            .cts_pin = 15u,
            .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP |
                         PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
            .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
        },
    },
    {
        .info = {
            .id = UART_PORT_4,
            .backend = UART_DRIVER_BACKEND_PIO,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 16u,
            .rx_pin = 17u,
        },
        .backend.pio = {
            .pio = pio1,
            .tx_state_machine = 0u,
            .rx_state_machine = 1u,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 16u,
            .rx_pin = 17u,
            .rts_pin = 18u,
            .cts_pin = 19u,
            .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP |
                         PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
            .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
        },
    },
    {
        .info = {
            .id = UART_PORT_5,
            .backend = UART_DRIVER_BACKEND_PIO,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 20u,
            .rx_pin = 21u,
        },
        .backend.pio = {
            .pio = pio1,
            .tx_state_machine = 2u,
            .rx_state_machine = 3u,
            .baud_rate = PICO_UART_BOARD_DEFAULT_BAUD_RATE,
            .tx_pin = 20u,
            .rx_pin = 21u,
            .rts_pin = 22u,
            .cts_pin = 26u,
            .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP |
                         PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
            .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
        },
    },
};
