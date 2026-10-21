/**
 * @file hw_uart_board_config.c
 * @brief Hardware-UART board resources and common-config resolution.
 */

#include "uart/hw/hw_uart_driver.h"

/**
 * @brief HW-only peripheral and optional-flow-control assignments for one port.
 */
typedef struct {
    uart_inst_t *instance; /**< Hardware UART peripheral. */
    bool hardware_flow_control; /**< Whether CTS and RTS are active. */
} hw_uart_driver_board_resources_t;

/** @brief Hardware UART resources indexed by logical port; non-HW ports are empty. */
static const hw_uart_driver_board_resources_t hw_uart_driver_board_resources[UART_PORT_COUNT] = {
    [UART_PORT_0] = {.instance = uart0, .hardware_flow_control = false},
    [UART_PORT_1] = {.instance = uart1, .hardware_flow_control = false},
};

bool hw_uart_driver_build_port_config(const uart_driver_port_info_t *port_info, hw_uart_driver_config_t *config)
{
    const hw_uart_driver_board_resources_t *resources;

    if ((port_info == NULL) || (config == NULL) || (port_info->id >= UART_PORT_COUNT) ||
        (port_info->backend != UART_DRIVER_BACKEND_HW) || (port_info->baud_rate == 0u) ||
        (port_info->data_bits < 5u) || (port_info->data_bits > 8u) ||
        ((port_info->stop_bits != 1u) && (port_info->stop_bits != 2u)) ||
        (port_info->parity > UART_DRIVER_PARITY_EVEN)) {
        return false;
    }

    resources = &hw_uart_driver_board_resources[port_info->id];
    if (resources->instance == NULL) {
        return false;
    }

    *config = (hw_uart_driver_config_t){
        .instance = resources->instance,
        .baud_rate = port_info->baud_rate,
        .tx_pin = port_info->tx_pin,
        .rx_pin = port_info->rx_pin,
        .cts_pin = port_info->cts_pin,
        .rts_pin = port_info->rts_pin,
        .hardware_flow_control = resources->hardware_flow_control,
        .data_bits = port_info->data_bits,
        .stop_bits = port_info->stop_bits,
        .parity = port_info->parity == UART_DRIVER_PARITY_ODD ? UART_PARITY_ODD
              : port_info->parity == UART_DRIVER_PARITY_EVEN ? UART_PARITY_EVEN
                                                                    : UART_PARITY_NONE,
    };
    return true;
}