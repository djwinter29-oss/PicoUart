/**
 * @file backend_policy.h
 * @brief Host-testable idle, DMA re-arm, and PIO TX policy for UART backends.
 *
 * Hardware register access stays in the HW/PIO drivers. This header locks the
 * boolean tables those drivers use for line-format apply, RX DMA poll re-arm,
 * and hybrid PIO TX.
 */

#ifndef UART_BACKEND_POLICY_H
#define UART_BACKEND_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Next PIO TX step after any in-flight DMA completion poll.
 */
typedef enum {
    UART_PIO_TX_KEEP_DMA = 0, /**< TX DMA is still active; do not touch the FIFO. */
    UART_PIO_TX_START_DMA, /**< Backlog crossed the DMA threshold; claim and launch. */
    UART_PIO_TX_DRAIN_FIFO, /**< Short queue, or DMA claim failed; fill the TX FIFO. */
} uart_pio_tx_action_t;

/**
 * @brief Return whether a hardware UART is idle enough to apply line format.
 * @param tx_occupancy_nonzero True when the USB-to-UART ring still holds bytes.
 * @param tx_dma_active True while a TX DMA transfer is in flight.
 * @param uart_busy True when UARTFR.BUSY is set.
 * @param rx_fifo_readable True when the UART RX FIFO still holds unread bytes.
 * @return `true` when the worker may pause RX DMA and reconfigure.
 */
static inline bool uart_hw_line_format_idle(bool tx_occupancy_nonzero,
                                            bool tx_dma_active,
                                            bool uart_busy,
                                            bool rx_fifo_readable)
{
    return !tx_occupancy_nonzero && !tx_dma_active && !uart_busy && !rx_fifo_readable;
}

/**
 * @brief Return whether a PIO UART is idle enough to start a baud-change pause.
 * @param tx_dma_active True while a TX DMA transfer is in flight.
 * @param tx_occupancy_nonzero True when the USB-to-UART ring still holds bytes.
 * @param tx_fifo_empty True when the PIO TX FIFO is empty.
 * @param tx_shifter_idle True when TXSTALL has re-asserted after write-clear.
 * @param rx_fifo_empty True when the PIO RX FIFO is empty.
 * @param rx_line_idle True when the RX pin is idle-high (or the check is disabled).
 * @return `true` when the worker may pause RX DMA and change baud.
 */
static inline bool uart_pio_baud_change_idle(bool tx_dma_active,
                                             bool tx_occupancy_nonzero,
                                             bool tx_fifo_empty,
                                             bool tx_shifter_idle,
                                             bool rx_fifo_empty,
                                             bool rx_line_idle)
{
    return !tx_dma_active && !tx_occupancy_nonzero && tx_fifo_empty && tx_shifter_idle &&
           rx_fifo_empty && rx_line_idle;
}

/**
 * @brief Return whether the poll-loop RX DMA safety net should reload TRANS_COUNT.
 * @param channel_valid True when the backend owns an RX DMA channel.
 * @param channel_busy True when the DMA channel BUSY bit is set.
 * @param remaining Masked TRANS_COUNT remaining.
 * @return `true` when the countdown has exhausted and the IRQ may have been missed.
 */
static inline bool uart_rx_dma_poll_should_rearm(bool channel_valid,
                                                 bool channel_busy,
                                                 uint32_t remaining)
{
    return channel_valid && !channel_busy && (remaining == 0u);
}

/**
 * @brief Select the PIO TX drain path for one worker sweep.
 * @param tx_dma_active True when a TX DMA transfer still owns a ring span.
 * @param occupancy TX ring occupancy in bytes.
 * @param dma_threshold Occupancy that prefers DMA over FIFO polling.
 * @return The next TX action. @ref UART_PIO_TX_START_DMA may still fall back to
 * FIFO drain when no DMA channel is available.
 */
static inline uart_pio_tx_action_t uart_pio_tx_action(bool tx_dma_active,
                                                      size_t occupancy,
                                                      size_t dma_threshold)
{
    if (tx_dma_active) {
        return UART_PIO_TX_KEEP_DMA;
    }

    if (occupancy >= dma_threshold) {
        return UART_PIO_TX_START_DMA;
    }

    return UART_PIO_TX_DRAIN_FIFO;
}

/**
 * @brief Bound a PIO TX DMA launch to the live ring occupancy.
 * @param occupancy TX ring occupancy in bytes.
 * @param max_transfer Configured per-launch maximum.
 * @return Bytes to program into TRANS_COUNT.
 */
static inline size_t uart_pio_tx_dma_transfer_bytes(size_t occupancy, size_t max_transfer)
{
    return (max_transfer > occupancy) ? occupancy : max_transfer;
}

#endif
