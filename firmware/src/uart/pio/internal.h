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

/** @brief Maximum number of logical PIO UART instances in the current topology. */
#define PIO_UART_DRIVER_MAX_INSTANCES 4u

/**
 * @brief Runtime state for one PIO UART instance.
 */
struct pio_uart_driver {
    pio_uart_driver_config_t config; /**< Immutable PIO UART configuration. */
    bool initialized; /**< True after the PIO state machines and software rings are configured. */
    int tx_dma_channel; /**< Claimed DMA channel used for high-backlog TX draining. */
    size_t tx_dma_bytes_in_flight; /**< Bytes currently owned by the active TX DMA transfer. */
    uint16_t tx_dma_retry_cooldown; /**< Worker polls to wait before retrying a failed TX DMA claim. */
    uint32_t tx_dma_claim_failure_count; /**< Number of times TX DMA could not be claimed when requested. */
    size_t tx_polled_bytes; /**< Bytes sent through the direct FIFO polling path. */
    size_t tx_dma_bytes; /**< Bytes sent through the TX DMA path. */
    ring_buffer_t rx_ring; /**< PIO RX producer ring shared with the USB bridge. */
    ring_buffer_t tx_ring; /**< USB-core TX producer ring drained by core-0 PIO polling. */
    size_t rx_framing_error_count; /**< Number of dropped RX words with an invalid stop bit. */
    uint8_t rx_storage[PICO_UART_PIO_UART_RX_BUFFER_SIZE]; /**< RX ring storage. */
    uint8_t tx_storage[PICO_UART_PIO_UART_TX_BUFFER_SIZE]; /**< TX ring storage. */
};

void pio_uart_driver_poll(pio_uart_driver_t *driver);

#endif
