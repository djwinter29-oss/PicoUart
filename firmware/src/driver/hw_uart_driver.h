/**
 * @file hw_uart_driver.h
 * @brief Hardware UART backend for PicoUart logical UART ports.
 */

#ifndef HW_UART_DRIVER_H
#define HW_UART_DRIVER_H

#include "hardware/uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Invalid GPIO marker used while board pin mapping is still open. */
#define UART_DRIVER_PIN_UNASSIGNED ((uint32_t)UINT32_MAX)

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
    size_t rx_read_index; /**< Software read pointer into the RX DMA ring buffer. */
    size_t tx_length; /**< Bytes currently queued in the TX DMA buffer. */
    bool tx_active; /**< True while a TX DMA transfer is still in flight. */
    uint8_t rx_buffer[256]; /**< DMA-owned circular RX buffer. */
    uint8_t tx_buffer[256]; /**< DMA source buffer for one TX burst. */
} hw_uart_driver_t;

/**
 * @brief Initialize one hardware UART backend.
 * @param driver Driver instance to initialize.
 * @return `true` when the UART was configured, otherwise `false`.
 */
bool hw_uart_driver_init(hw_uart_driver_t *driver);

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
 * @brief Write bytes to one hardware UART backend.
 * @param driver Driver instance to write to.
 * @param data Source bytes.
 * @param length Number of bytes to write.
 * @return Number of bytes written.
 */
size_t hw_uart_driver_write(hw_uart_driver_t *driver, const uint8_t *data, size_t length);

#endif