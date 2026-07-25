/**
 * @file driver.h
 * @brief Hardware UART backend for PicoUart logical UART ports.
 */

#ifndef HW_UART_DRIVER_H
#define HW_UART_DRIVER_H

#include "hardware/uart.h"
#include "uart/ring_buffer/ring_buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Invalid GPIO marker used while board pin mapping is still open. */
#define UART_DRIVER_PIN_UNASSIGNED ((uint32_t)UINT32_MAX)
/** @brief Hardware UART RX ring size in bytes. */
#define HW_UART_DRIVER_RX_BUFFER_SIZE 1024u
/** @brief Hardware UART TX ring size in bytes. */
#define HW_UART_DRIVER_TX_BUFFER_SIZE 1024u

/**
 * @brief Static configuration for one hardware UART instance.
 */
typedef struct {
    uart_inst_t *instance; /**< RP2040/RP2350 hardware UART peripheral. */
    uint32_t baud_rate; /**< Initial baud rate to program into the peripheral. */
    uint32_t tx_pin; /**< GPIO used for TX, or @ref UART_DRIVER_PIN_UNASSIGNED. */
    uint32_t rx_pin; /**< GPIO used for RX, or @ref UART_DRIVER_PIN_UNASSIGNED. */
    uint8_t data_bits; /**< UART data bits, typically 8. */
    uint8_t stop_bits; /**< UART stop bits, typically 1. */
    uart_parity_t parity; /**< UART parity mode. */
} hw_uart_driver_config_t;

/**
 * @brief Runtime state for one hardware UART instance.
 */
typedef struct {
    hw_uart_driver_config_t config; /**< Immutable UART configuration. */
    bool initialized; /**< True after the peripheral has been configured. */
    int rx_dma_channel; /**< Claimed DMA channel used for UART RX. */
    int tx_dma_channel; /**< Claimed DMA channel used for UART TX. */
    size_t tx_dma_bytes_in_flight; /**< Bytes currently owned by the active TX DMA transfer. */
    bool tx_active; /**< True while a TX DMA transfer is still in flight. */
    ring_buffer_t rx_ring; /**< UART-to-USB receive ring. */
    ring_buffer_t tx_ring; /**< USB-to-UART transmit ring. */
    uint8_t rx_storage[HW_UART_DRIVER_RX_BUFFER_SIZE] __attribute__((aligned(HW_UART_DRIVER_RX_BUFFER_SIZE))); /**< DMA-owned RX ring storage. */
    uint8_t tx_storage[HW_UART_DRIVER_TX_BUFFER_SIZE]; /**< TX ring storage. */
} hw_uart_driver_t;

/**
 * @brief Initialize one hardware UART backend.
 * @param driver Driver instance to initialize.
 * @return `true` when the UART was configured, otherwise `false`.
 */
bool hw_uart_driver_init(hw_uart_driver_t *driver);

/**
 * @brief Poll one hardware UART backend to advance TX DMA completion state.
 * @param driver Driver instance to poll.
 */
void hw_uart_driver_poll(hw_uart_driver_t *driver);

/**
 * @brief Deinitialize one hardware UART backend.
 * @param driver Driver instance to deinitialize.
 */
void hw_uart_driver_deinit(hw_uart_driver_t *driver);

/**
 * @brief Read bytes from one hardware UART backend.
 * @param driver Driver instance to read from.
 * @param data Destination buffer.
 * @param capacity Maximum bytes to read.
 * @return Number of bytes copied into @p data.
 */
size_t hw_uart_driver_read(hw_uart_driver_t *driver, uint8_t *data, size_t capacity);

/**
 * @brief Return the currently available TX ring space for one hardware UART backend.
 * @param driver Driver instance to inspect.
 * @return Number of bytes that can currently be queued for transmission.
 */
size_t hw_uart_driver_write_available(const hw_uart_driver_t *driver);

/**
 * @brief Reconfigure the baud rate for one hardware UART backend.
 * @param driver Driver instance to update.
 * @param baud_rate New baud rate.
 * @return `true` when the baud rate was applied, otherwise `false`.
 */
bool hw_uart_driver_set_baud_rate(hw_uart_driver_t *driver, uint32_t baud_rate);

/**
 * @brief Reconfigure the full UART line format for one hardware UART backend.
 * @param driver Driver instance to update.
 * @param baud_rate New baud rate.
 * @param data_bits New UART data-bit count.
 * @param stop_bits New UART stop-bit count.
 * @param parity New UART parity mode.
 * @return `true` when the line format was applied, otherwise `false`.
 */
bool hw_uart_driver_set_line_format(hw_uart_driver_t *driver,
                                    uint32_t baud_rate,
                                    uint8_t data_bits,
                                    uint8_t stop_bits,
                                    uart_parity_t parity);

/**
 * @brief Write bytes to one hardware UART backend.
 * @param driver Driver instance to write to.
 * @param data Source bytes.
 * @param length Number of bytes to write.
 * @return Number of bytes written.
 */
size_t hw_uart_driver_write(hw_uart_driver_t *driver, const uint8_t *data, size_t length);

#endif