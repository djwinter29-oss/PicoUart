/**
 * @file topology.c
 * @brief Host-testable UART board-topology validation.
 */

#include "uart/topology.h"

bool uart_topology_validate(const uart_topology_port_t *ports, size_t port_count)
{
    size_t hw_count = 0u;
    size_t pio_count = 0u;

    if ((ports == NULL) || (port_count != UART_PORT_COUNT)) {
        return false;
    }

    for (size_t index = 0u; index < port_count; ++index) {
        const uart_topology_port_t *port = &ports[index];

        if ((port->id != (uart_port_id_t)index) ||
            (port->tx_pin == UART_TOPOLOGY_PIN_UNASSIGNED) ||
            (port->rx_pin == UART_TOPOLOGY_PIN_UNASSIGNED) ||
            (port->tx_pin == port->rx_pin) ||
            (port->backend_instance == 0u) ||
            (port->backend_tx_pin != port->tx_pin) ||
            (port->backend_rx_pin != port->rx_pin) ||
            (port->backend_baud_rate != port->baud_rate)) {
            return false;
        }

        for (size_t prior = 0u; prior < index; ++prior) {
            const uart_topology_port_t *previous = &ports[prior];

            if ((port->tx_pin == previous->tx_pin) ||
                (port->tx_pin == previous->rx_pin) ||
                (port->rx_pin == previous->tx_pin) ||
                (port->rx_pin == previous->rx_pin)) {
                return false;
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