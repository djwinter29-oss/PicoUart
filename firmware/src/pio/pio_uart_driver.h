/**
 * @file pio_uart_driver.h
 * @brief PIO UART backend for PicoUart logical UART ports.
 */

#ifndef PIO_UART_DRIVER_H
#define PIO_UART_DRIVER_H

#include "hardware/pio.h"
#include "ring_buffer/ring_buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Invalid GPIO marker used while board pin mapping is still open. */
#define PIO_UART_DRIVER_PIN_UNASSIGNED ((uint32_t)UINT32_MAX)
/** @brief PIO UART RX ring size in bytes. */
#define PIO_UART_DRIVER_RX_BUFFER_SIZE 1024u
/** @brief PIO UART TX ring size in bytes. */
#define PIO_UART_DRIVER_TX_BUFFER_SIZE 1024u

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
} pio_uart_driver_config_t;

/**
 * @brief Runtime state for one PIO UART instance.
 */
typedef struct {
    pio_uart_driver_config_t config; /**< Immutable PIO UART configuration. */
    bool initialized; /**< True after a real PIO program is attached. */
    ring_buffer_t rx_ring; /**< UART-to-USB receive ring. */
    ring_buffer_t tx_ring; /**< USB-to-UART transmit ring. */
    uint8_t rx_storage[PIO_UART_DRIVER_RX_BUFFER_SIZE]; /**< RX ring storage. */
    uint8_t tx_storage[PIO_UART_DRIVER_TX_BUFFER_SIZE]; /**< TX ring storage. */
} pio_uart_driver_t;

/**
 * @brief Initialize one PIO UART backend.
 * @param driver Driver instance to initialize.
 * @return `true` when the UART was configured, otherwise `false`.
 */
bool pio_uart_driver_init(pio_uart_driver_t *driver);

/**
 * @brief Poll one PIO UART backend to move data between FIFOs and rings.
 * @param driver Driver instance to poll.
 */
void pio_uart_driver_poll(pio_uart_driver_t *driver);

/**
 * @brief Deinitialize one PIO UART backend.
 * @param driver Driver instance to deinitialize.
 */
void pio_uart_driver_deinit(pio_uart_driver_t *driver);

/**
 * @brief Read bytes from one PIO UART backend.
 * @param driver Driver instance to read from.
 * @param data Destination buffer.
 * @param capacity Maximum bytes to read.
 * @return Number of bytes copied into @p data.
 */
size_t pio_uart_driver_read(pio_uart_driver_t *driver, uint8_t *data, size_t capacity);

/**
 * @brief Return the currently available TX ring space for one PIO UART backend.
 * @param driver Driver instance to inspect.
 * @return Number of bytes that can currently be queued for transmission.
 */
size_t pio_uart_driver_write_available(const pio_uart_driver_t *driver);

/**
 * @brief Reconfigure the baud rate for one PIO UART backend.
 * @param driver Driver instance to update.
 * @param baud_rate New baud rate.
 * @return `true` when the baud rate was applied, otherwise `false`.
 */
bool pio_uart_driver_set_baud_rate(pio_uart_driver_t *driver, uint32_t baud_rate);

/**
 * @brief Write bytes to one PIO UART backend.
 * @param driver Driver instance to write to.
 * @param data Source bytes.
 * @param length Number of bytes to write.
 * @return Number of bytes written.
 */
size_t pio_uart_driver_write(pio_uart_driver_t *driver, const uint8_t *data, size_t length);

#endif