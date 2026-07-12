/**
 * @file uart_driver.c
 * @brief Logical UART port table for the PicoUart firmware.
 */

#include "driver/uart_driver.h"

#include "driver/hw_uart_driver.h"
#include "driver/pio_uart_driver.h"

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
            .config = {pio0, 0u, UART_DRIVER_DEFAULT_BAUD_RATE, 8u, 9u},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_3, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 12u, 13u},
        .backend.pio = {
            .config = {pio0, 1u, UART_DRIVER_DEFAULT_BAUD_RATE, 12u, 13u},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_4, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 16u, 17u},
        .backend.pio = {
            .config = {pio1, 0u, UART_DRIVER_DEFAULT_BAUD_RATE, 16u, 17u},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_5, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 20u, 21u},
        .backend.pio = {
            .config = {pio1, 1u, UART_DRIVER_DEFAULT_BAUD_RATE, 20u, 21u},
            .initialized = false,
        },
    },
};

size_t uart_driver_port_count(void)
{
    return UART_PORT_COUNT;
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