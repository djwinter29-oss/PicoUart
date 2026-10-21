/**
 * @file pio_uart_board_config.c
 * @brief PIO-UART board resources and common-config resolution.
 */

#include "uart/pio/pio_uart_driver.h"

/**
 * @brief PIO-only block, state-machine, and auxiliary-pin assignments for one port.
 */
typedef struct {
    PIO pio; /**< PIO block. */
    uint32_t tx_state_machine; /**< TX state machine. */
    uint32_t rx_state_machine; /**< RX state machine. */
    uint32_t pin_flags; /**< Backend GPIO policy flags. */
    uint32_t tx_dma_start_threshold; /**< TX DMA launch threshold. */
} pio_uart_driver_board_resources_t;

/** @brief PIO resources indexed by logical port; non-PIO ports are empty. */
static const pio_uart_driver_board_resources_t pio_uart_driver_board_resources[UART_PORT_COUNT] = {
    [UART_PORT_2] = {
        .pio = pio0,
        .tx_state_machine = 0u,
        .rx_state_machine = 1u,
        .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
        .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
    },
    [UART_PORT_3] = {
        .pio = pio0,
        .tx_state_machine = 2u,
        .rx_state_machine = 3u,
        .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
        .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
    },
    [UART_PORT_4] = {
        .pio = pio1,
        .tx_state_machine = 0u,
        .rx_state_machine = 1u,
        .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
        .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
    },
    [UART_PORT_5] = {
        .pio = pio1,
        .tx_state_machine = 2u,
        .rx_state_machine = 3u,
        .pin_flags = PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP | PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
        .tx_dma_start_threshold = PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD,
    },
};

bool pio_uart_driver_build_port_config(const uart_driver_port_info_t *port_info, pio_uart_driver_config_t *config)
{
    const pio_uart_driver_board_resources_t *resources;

    if ((port_info == NULL) || (config == NULL) || (port_info->id >= UART_PORT_COUNT) ||
        (port_info->baud_rate == 0u) || (port_info->backend != UART_DRIVER_BACKEND_PIO)) {
        return false;
    }

    resources = &pio_uart_driver_board_resources[port_info->id];
    if (resources->pio == NULL) {
        return false;
    }

    if ((port_info->data_bits != 8u) || (port_info->stop_bits != 1u) ||
        (port_info->parity != UART_DRIVER_PARITY_NONE)) {
        return false;
    }

    *config = (pio_uart_driver_config_t){
        .pio = resources->pio,
        .tx_state_machine = resources->tx_state_machine,
        .rx_state_machine = resources->rx_state_machine,
        .baud_rate = port_info->baud_rate,
        .tx_pin = port_info->tx_pin,
        .rx_pin = port_info->rx_pin,
        .rts_pin = port_info->rts_pin,
        .cts_pin = port_info->cts_pin,
        .pin_flags = resources->pin_flags,
        .tx_dma_start_threshold = resources->tx_dma_start_threshold,
    };
    return true;
}