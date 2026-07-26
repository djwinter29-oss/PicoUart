/**
 * @file driver.h
 * @brief Hardware UART backend for PicoUart logical UART ports.
 */

#ifndef HW_UART_DRIVER_H
#define HW_UART_DRIVER_H

#include "config/config.h"
#include "hardware/uart.h"
#include "uart/ring_buffer/ring_buffer.h"

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
    uint32_t cts_pin; /**< GPIO used for CTS, or @ref UART_DRIVER_PIN_UNASSIGNED. */
    uint32_t rts_pin; /**< GPIO used for RTS, or @ref UART_DRIVER_PIN_UNASSIGNED. */
    bool hardware_flow_control; /**< Enable CTS/RTS when both flow-control pins are assigned. */
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
    uint32_t controller_tx_bytes; /**< Bytes completed by the UART TX DMA engine. */
    uint32_t controller_rx_bytes; /**< Bytes accepted by the UART RX DMA engine. */
    uint32_t rx_error_count; /**< Hardware UART receive-status events observed since initialization. */
    uint32_t rx_dma_last_progress; /**< Last current-transfer RX DMA progress used for accounting. */
    ring_buffer_t rx_ring; /**< UART-to-USB receive ring. */
    ring_buffer_t tx_ring; /**< USB-to-UART transmit ring. */
    uint8_t rx_storage[PICO_UART_HW_UART_RX_BUFFER_SIZE] __attribute__((aligned(PICO_UART_HW_UART_RX_BUFFER_SIZE))); /**< DMA-owned RX ring storage. */
    uint8_t tx_storage[PICO_UART_HW_UART_TX_BUFFER_SIZE]; /**< TX ring storage. */
} hw_uart_driver_t;

/**
 * @brief Initialize one hardware UART backend.
 * @param driver Driver instance to initialize.
 * @return `true` when the UART was configured, otherwise `false`.
 */
bool hw_uart_driver_init(hw_uart_driver_t *driver);

/**
 * @brief Activate hardware-UART RX DMA interrupts on the UART worker core.
 *
 * Call after all hardware UART backends are initialized and from the core that
 * owns steady-state UART service.
 */
void hw_uart_driver_enable_rx_dma_irq(void);

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

#endif