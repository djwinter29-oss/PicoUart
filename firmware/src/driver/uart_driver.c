/**
 * @file uart_driver.c
 * @brief Logical UART port table for the PicoUart firmware.
 */

#include "driver/uart_driver.h"

#include "driver/hw_uart_driver.h"
#include "pio/pio_uart_driver.h"

/** @brief Default startup baud rate applied to all logical UART ports. */
#define UART_DRIVER_DEFAULT_BAUD_RATE 115200u

/**
 * @brief Runtime storage for one logical UART port.
 */
typedef struct {
    uart_driver_port_info_t info; /**< Public metadata for the logical port. */
    union {
        hw_uart_driver_t hw; /**< Hardware UART backend state. */
        pio_uart_driver_t pio; /**< PIO UART backend state. */
    } backend; /**< Backend storage owned by the logical port. */
} uart_driver_port_t;

/** @brief Static logical UART port table for the 6-port bridge. */
static uart_driver_port_t uart_ports[UART_PORT_COUNT] = {
    {
        .info = {UART_PORT_0, UART_DRIVER_BACKEND_HW, UART_DRIVER_DEFAULT_BAUD_RATE, 0u, 1u},
        .backend.hw = {
            .config = {uart0, UART_DRIVER_DEFAULT_BAUD_RATE, 0u, 1u, 8u, 1u, UART_PARITY_NONE},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_1, UART_DRIVER_BACKEND_HW, UART_DRIVER_DEFAULT_BAUD_RATE, 4u, 5u},
        .backend.hw = {
            .config = {uart1, UART_DRIVER_DEFAULT_BAUD_RATE, 4u, 5u, 8u, 1u, UART_PARITY_NONE},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_2, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 8u, 9u},
        .backend.pio = {
            .config = {pio0, 0u, 1u, UART_DRIVER_DEFAULT_BAUD_RATE, 8u, 9u},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_3, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 12u, 13u},
        .backend.pio = {
            .config = {pio0, 2u, 3u, UART_DRIVER_DEFAULT_BAUD_RATE, 12u, 13u},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_4, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 16u, 17u},
        .backend.pio = {
            .config = {pio1, 0u, 1u, UART_DRIVER_DEFAULT_BAUD_RATE, 16u, 17u},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_5, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 20u, 21u},
        .backend.pio = {
            .config = {pio1, 2u, 3u, UART_DRIVER_DEFAULT_BAUD_RATE, 20u, 21u},
            .initialized = false,
        },
    },
};

static uart_driver_port_t *uart_driver_port_mutable(uart_port_id_t port_id)
{
    if (port_id >= UART_PORT_COUNT) {
        return NULL;
    }

    return &uart_ports[port_id];
}

size_t uart_driver_port_count(void)
{
    return UART_PORT_COUNT;
}

void uart_driver_init(void)
{
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_driver_port_t *port = &uart_ports[index];

        if (port->info.backend == UART_DRIVER_BACKEND_HW) {
            if (!port->backend.hw.initialized) {
                (void)hw_uart_driver_init(&port->backend.hw);
            }
            continue;
        }

        if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
            if (!port->backend.pio.initialized) {
                (void)pio_uart_driver_init(&port->backend.pio);
            }
        }
    }
}

void uart_driver_poll(void)
{
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_driver_port_t *port = &uart_ports[index];

        if (port->info.backend == UART_DRIVER_BACKEND_HW) {
            hw_uart_driver_poll(&port->backend.hw);
            continue;
        }

        if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
            pio_uart_driver_poll(&port->backend.pio);
        }
    }
}

bool uart_driver_port_is_ready(uart_port_id_t port_id)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);

    if (port == NULL) {
        return false;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        return port->backend.hw.initialized;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        return port->backend.pio.initialized;
    }

    return false;
}

size_t uart_driver_read(uart_port_id_t port_id, uint8_t *data, size_t capacity)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);

    if (port == NULL) {
        return 0u;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        return hw_uart_driver_read(&port->backend.hw, data, capacity);
    }

    if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        return pio_uart_driver_read(&port->backend.pio, data, capacity);
    }

    return 0u;
}

size_t uart_driver_write_available(uart_port_id_t port_id)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);

    if (port == NULL) {
        return 0u;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        return hw_uart_driver_write_available(&port->backend.hw);
    }

    if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        return pio_uart_driver_write_available(&port->backend.pio);
    }

    return 0u;
}

bool uart_driver_set_baud_rate(uart_port_id_t port_id, uint32_t baud_rate)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);

    if (port == NULL) {
        return false;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        if (!hw_uart_driver_set_baud_rate(&port->backend.hw, baud_rate)) {
            return false;
        }
    } else if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        if (!pio_uart_driver_set_baud_rate(&port->backend.pio, baud_rate)) {
            return false;
        }
    } else {
        return false;
    }

    port->info.baud_rate = baud_rate;
    return true;
}

size_t uart_driver_write(uart_port_id_t port_id, const uint8_t *data, size_t length)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);

    if (port == NULL) {
        return 0u;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        return hw_uart_driver_write(&port->backend.hw, data, length);
    }

    if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        return pio_uart_driver_write(&port->backend.pio, data, length);
    }

    return 0u;
}

const uart_driver_port_info_t *uart_driver_port_info(uart_port_id_t port_id)
{
    if (port_id >= UART_PORT_COUNT) {
        return NULL;
    }

    return &uart_ports[port_id].info;
}

bool uart_driver_validate_topology(void)
{
    size_t hw_count = 0u;
    size_t pio_count = 0u;

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        if (uart_ports[index].info.id != (uart_port_id_t)index) {
            return false;
        }

        if (uart_ports[index].info.backend == UART_DRIVER_BACKEND_HW) {
            hw_count += 1u;
            continue;
        }

        if (uart_ports[index].info.backend == UART_DRIVER_BACKEND_PIO) {
            pio_count += 1u;
            continue;
        }

        return false;
    }

    return (hw_count == 2u) && (pio_count == 4u);
}