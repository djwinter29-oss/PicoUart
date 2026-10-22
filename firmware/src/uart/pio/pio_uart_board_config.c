/**
 * @file pio_uart_board_config.c
 * @brief Compile-time PIO-UART assignments referencing the common board table.
 */

#include "uart/pio/pio_uart_driver.h"

const pio_uart_driver_config_t pio_uart_driver_configs[UART_PORT_COUNT] = {
    [UART_PORT_2] = {
        .port = &uart_board_ports[UART_PORT_2],
        .pio = pio0,
        .tx_state_machine = 0u,
        .rx_state_machine = 1u,
        .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
        .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
    },
    [UART_PORT_3] = {
        .port = &uart_board_ports[UART_PORT_3],
        .pio = pio0,
        .tx_state_machine = 2u,
        .rx_state_machine = 3u,
        .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
        .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
    },
    [UART_PORT_4] = {
        .port = &uart_board_ports[UART_PORT_4],
        .pio = pio1,
        .tx_state_machine = 0u,
        .rx_state_machine = 1u,
        .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
        .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
    },
    [UART_PORT_5] = {
        .port = &uart_board_ports[UART_PORT_5],
        .pio = pio1,
        .tx_state_machine = 2u,
        .rx_state_machine = 3u,
        .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
        .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
    },
};