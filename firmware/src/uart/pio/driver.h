/**
 * @file driver.h
 * @brief PIO UART backend for PicoUart logical UART ports.
 */

#ifndef PIO_UART_DRIVER_H
#define PIO_UART_DRIVER_H

#include "config/config.h"
#include "hardware/pio.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Invalid GPIO marker used while board pin mapping is still open. */
#define PIO_UART_DRIVER_PIN_UNASSIGNED ((uint32_t)UINT32_MAX)
/** @brief No optional board-level GPIO policy flags are enabled. */
#define PIO_UART_DRIVER_PIN_FLAG_NONE 0u
/** @brief Enable a pull-up on the configured RX pin during backend init. */
#define PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP (1u << 0)
/** @brief Require a high (idle) RX line as an extra guard before applying a deferred baud change. */
#define PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH (1u << 1)
/**
 * @brief Static configuration for one PIO UART instance.
 */
typedef struct {
    PIO pio; /**< PIO block assigned to the logical UART. */
    uint32_t tx_state_machine; /**< PIO TX state machine index. */
    uint32_t rx_state_machine; /**< PIO RX state machine index. */
    uint32_t baud_rate; /**< Target UART baud rate. */
    uint32_t tx_pin; /**< GPIO used for TX, or @ref PIO_UART_DRIVER_PIN_UNASSIGNED. */
    uint32_t rx_pin; /**< GPIO used for RX, or @ref PIO_UART_DRIVER_PIN_UNASSIGNED. */
    uint32_t pin_flags; /**< Explicit board-level GPIO policy flags. */
    uint32_t tx_dma_start_threshold; /**< TX backlog threshold that triggers a DMA transfer, or 0 for the default. */
} pio_uart_driver_config_t;

/**
 * @brief Opaque runtime state for one PIO UART instance.
 */
typedef struct pio_uart_driver pio_uart_driver_t;

/**
 * @brief Initialize one PIO UART backend.
 * @param driver Driver instance to initialize.
 * @return `true` when the UART was configured, otherwise `false`.
 */
bool pio_uart_driver_init(pio_uart_driver_t *driver);

/**
 * @brief Activate PIO-UART RX DMA interrupts on the UART worker core.
 *
 * Call after all PIO UART backends are initialized and from the core that owns
 * steady-state UART service.
 */
void pio_uart_driver_enable_rx_dma_irq(void);

/**
 * @brief Poll one PIO UART backend to publish RX DMA progress and advance TX service.
 * RX bytes are moved by DMA from the PIO RX FIFO into the shared ring; the poll
 * path publishes producer progress and re-arms exhausted transfers. TX uses FIFO
 * polling for short queues and DMA for deeper backlog.
 * @param driver Driver instance to poll.
 */
void pio_uart_driver_poll(pio_uart_driver_t *driver);

/**
 * @brief Deinitialize one PIO UART backend.
 * @param driver Driver instance to deinitialize.
 */
void pio_uart_driver_deinit(pio_uart_driver_t *driver);

/**
 * @brief Reconfigure the baud rate for one PIO UART backend.
 * @param driver Driver instance to update.
 * @param baud_rate New baud rate.
 * @return `true` when the baud rate was applied, otherwise `false`.
 */
bool pio_uart_driver_set_baud_rate(pio_uart_driver_t *driver, uint32_t baud_rate);

#endif