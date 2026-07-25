/**
 * @file internal.h
 * @brief Internal runtime state for the PicoUart PIO UART backend.
 */

#ifndef PIO_UART_DRIVER_INTERNAL_H
#define PIO_UART_DRIVER_INTERNAL_H

#include "uart/pio/driver.h"
#include "uart/ring_buffer/ring_buffer.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Runtime state for one PIO UART instance.
 */
struct pio_uart_driver {
    pio_uart_driver_config_t config; /**< Immutable PIO UART configuration. */
    bool initialized; /**< True after the PIO state machines and software rings are configured. */
    int tx_dma_channel; /**< Dynamically claimed DMA channel used for high-backlog TX draining. */
    bool tx_dma_active; /**< True while the active TX DMA channel owns a ring span. */
    size_t tx_dma_bytes_in_flight; /**< Bytes currently owned by the active TX DMA transfer. */
    size_t tx_polled_bytes; /**< Bytes sent through the direct FIFO polling path. */
    size_t tx_dma_bytes; /**< Bytes sent through the TX DMA path. */
    uint32_t controller_rx_bytes; /**< Valid received bytes removed from the PIO RX FIFO. */
    ring_buffer_t rx_ring; /**< PIO RX producer ring shared with the USB bridge. */
    ring_buffer_t tx_ring; /**< USB-core TX producer ring drained by core-1 PIO polling. */
    uint8_t rx_storage[PICO_UART_PIO_UART_RX_BUFFER_SIZE]; /**< RX ring storage. */
    uint8_t tx_storage[PICO_UART_PIO_UART_TX_BUFFER_SIZE]; /**< TX ring storage. */
};

void pio_uart_driver_poll(pio_uart_driver_t *driver);

#endif
