/**
 * @file runtime.h
 * @brief Private runtime state shared by UART orchestration modules.
 */

#ifndef UART_RUNTIME_H
#define UART_RUNTIME_H

#include "uart/backend/backend.h"
#include "uart/hw/hw_uart_driver.h"
#include "uart/pio/pio_uart_driver_internal.h"
#include "uart/uart_driver.h"

/** @brief Storage for the selected concrete driver, owned by one logical port. */
typedef union {
    hw_uart_driver_t hw; /**< Hardware UART runtime state. */
    pio_uart_driver_t pio; /**< PIO UART runtime state. */
} uart_backend_instance_t;

/**
 * @brief Runtime storage for one logical UART port.
 */
typedef struct {
    uart_driver_port_info_t info;    /**< Public metadata for the logical port. */
    const uart_backend_ops_t *ops;   /**< Operations for the active backend storage. */
    uart_backend_instance_t backend; /**< Backend storage owned by the logical port. */
} uart_runtime_port_t;

#endif