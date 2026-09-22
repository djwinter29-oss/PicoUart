/**
 * @file uart_board.c
 * @brief PicoUart board-specific UART pin and peripheral mapping.
 */

#include "board/uart_board.h"

#include "hardware/gpio.h"

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

bool uart_board_validate_topology(void)
{
    uart_board_topology_port_t topology[UART_PORT_COUNT];

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        const uart_board_port_config_t *board = &uart_board_ports[index];

        if (board->info.backend == UART_DRIVER_BACKEND_HW) {
            topology[index] = (uart_board_topology_port_t){
                .id = board->info.id,
                .backend = board->info.backend,
                .baud_rate = board->info.baud_rate,
                .tx_pin = board->info.tx_pin,
                .rx_pin = board->info.rx_pin,
                .backend_instance = (uintptr_t)board->backend.hw.instance,
                .backend_baud_rate = board->backend.hw.baud_rate,
                .backend_tx_pin = board->backend.hw.tx_pin,
                .backend_rx_pin = board->backend.hw.rx_pin,
                .target_gpio_count = NUM_BANK0_GPIOS,
                .rts_pin = board->backend.hw.rts_pin,
                .cts_pin = board->backend.hw.cts_pin,
                .rts_enabled = board->backend.hw.hardware_flow_control,
                .cts_enabled = board->backend.hw.hardware_flow_control,
            };
        } else if (board->info.backend == UART_DRIVER_BACKEND_PIO) {
            topology[index] = (uart_board_topology_port_t){
                .id = board->info.id,
                .backend = board->info.backend,
                .baud_rate = board->info.baud_rate,
                .tx_pin = board->info.tx_pin,
                .rx_pin = board->info.rx_pin,
                .backend_instance = (uintptr_t)board->backend.pio.pio,
                .backend_baud_rate = board->backend.pio.baud_rate,
                .backend_tx_pin = board->backend.pio.tx_pin,
                .backend_rx_pin = board->backend.pio.rx_pin,
                .tx_state_machine = board->backend.pio.tx_state_machine,
                .rx_state_machine = board->backend.pio.rx_state_machine,
                .target_gpio_count = NUM_BANK0_GPIOS,
                .rts_pin = board->backend.pio.rts_pin,
                .cts_pin = board->backend.pio.cts_pin,
                .rts_enabled = (board->backend.pio.pin_flags &
                                PIO_UART_DRIVER_PIN_FLAG_RX_FLOW_CONTROL) != 0u,
                .cts_enabled = (board->backend.pio.pin_flags &
                                PIO_UART_DRIVER_PIN_FLAG_TX_FLOW_CONTROL) != 0u,
            };
        } else {
            return false;
        }
    }

    return uart_board_topology_validate(topology, UART_PORT_COUNT);
}
