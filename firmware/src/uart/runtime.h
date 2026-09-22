/**
 * @file runtime.h
 * @brief Private runtime state shared by UART orchestration modules.
 */

#ifndef UART_RUNTIME_H
#define UART_RUNTIME_H

#include "uart/backend/adapter.h"
#include "uart/hw/hw_uart_driver.h"
#include "uart/pio/pio_uart_driver_internal.h"
#include "uart/uart_driver.h"

/**
 * @brief Runtime storage for one logical UART port.
 */
typedef struct {
    uart_driver_port_info_t info; /**< Public metadata for the logical port. */
    const uart_backend_ops_t *ops; /**< Operations for the active backend storage. */
    union {
        hw_uart_driver_t hw; /**< Hardware UART backend state. */
        pio_uart_driver_t pio; /**< PIO UART backend state. */
    } backend; /**< Backend storage owned by the logical port. */
} uart_runtime_port_t;

#endif