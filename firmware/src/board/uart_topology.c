/**
 * @file uart_topology.c
 * @brief Host-testable validation for UART board resource assignments.
 */

#include "board/uart_topology.h"

static bool uart_board_topology_pin_valid(uint32_t pin, uint32_t gpio_count)
{
    return (pin != UART_BOARD_TOPOLOGY_PIN_UNASSIGNED) && (pin < gpio_count);
}

bool uart_board_topology_validate(const uart_board_topology_port_t *ports, size_t port_count)
{
    size_t hw_count = 0u;
    size_t pio_count = 0u;

    if ((ports == NULL) || (port_count != UART_PORT_COUNT)) {
        return false;
    }

    for (size_t index = 0u; index < port_count; ++index) {
        const uart_board_topology_port_t *port = &ports[index];
        uint32_t pins[4] = {port->tx_pin, port->rx_pin, port->rts_pin, port->cts_pin};
        size_t pin_count = 2u;

        if ((port->id != (uart_port_id_t)index) ||
            !uart_board_topology_pin_valid(port->tx_pin, port->target_gpio_count) ||
            !uart_board_topology_pin_valid(port->rx_pin, port->target_gpio_count) ||
            (port->tx_pin == port->rx_pin) ||
            (port->backend_instance == 0u) ||
            (port->backend_tx_pin != port->tx_pin) ||
            (port->backend_rx_pin != port->rx_pin) ||
            (port->backend_baud_rate != port->baud_rate)) {
            return false;
        }

        if (port->rts_enabled) {
            if (!uart_board_topology_pin_valid(port->rts_pin, port->target_gpio_count)) {
                return false;
            }
            pins[pin_count++] = port->rts_pin;
        }
        if (port->cts_enabled) {
            if (!uart_board_topology_pin_valid(port->cts_pin, port->target_gpio_count)) {
                return false;
            }
            pins[pin_count++] = port->cts_pin;
        }

        for (size_t pin = 0u; pin < pin_count; ++pin) {
            for (size_t other = 0u; other < pin; ++other) {
                if (pins[pin] == pins[other]) {
                    return false;
                }
            }
        }

        for (size_t prior = 0u; prior < index; ++prior) {
            const uart_board_topology_port_t *previous = &ports[prior];
            uint32_t previous_pins[4] = {
                previous->tx_pin, previous->rx_pin, previous->rts_pin, previous->cts_pin};
            size_t previous_pin_count = 2u + (previous->rts_enabled ? 1u : 0u) +
                                        (previous->cts_enabled ? 1u : 0u);

            if (previous->rts_enabled && previous->cts_enabled) {
                previous_pins[2] = previous->rts_pin;
                previous_pins[3] = previous->cts_pin;
            } else if (previous->rts_enabled) {
                previous_pins[2] = previous->rts_pin;
            } else if (previous->cts_enabled) {
                previous_pins[2] = previous->cts_pin;
            }

            for (size_t pin = 0u; pin < pin_count; ++pin) {
                for (size_t previous_pin = 0u; previous_pin < previous_pin_count; ++previous_pin) {
                    if (pins[pin] == previous_pins[previous_pin]) {
                        return false;
                    }
                }
            }

            if ((port->backend == previous->backend) &&
                (port->backend_instance == previous->backend_instance)) {
                if (port->backend == UART_DRIVER_BACKEND_HW) {
                    return false;
                }

                if ((port->backend == UART_DRIVER_BACKEND_PIO) &&
                    ((port->tx_state_machine == previous->tx_state_machine) ||
                     (port->tx_state_machine == previous->rx_state_machine) ||
                     (port->rx_state_machine == previous->tx_state_machine) ||
                     (port->rx_state_machine == previous->rx_state_machine))) {
                    return false;
                }
            }
        }

        if (port->backend == UART_DRIVER_BACKEND_HW) {
            hw_count += 1u;
        } else if ((port->backend == UART_DRIVER_BACKEND_PIO) &&
                   (port->tx_state_machine < 4u) &&
                   (port->rx_state_machine < 4u) &&
                   (port->tx_state_machine != port->rx_state_machine)) {
            pio_count += 1u;
        } else {
            return false;
        }
    }

    return (hw_count == 2u) && (pio_count == 4u);
}