/**
 * @file backend_policy.h
 * @brief Host-testable idle, DMA re-arm, and PIO TX policy for UART backends.
 *
 * Hardware register access stays in the HW/PIO drivers. This header locks the
 * boolean tables those drivers use for line-format apply, RX DMA re-arm, and
 * bounded TX DMA launches.
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
    UART_PIO_TX_START_DMA, /**< Backlog crossed the DMA threshold; launch the persistent TX channel. */
    UART_PIO_TX_DRAIN_FIFO, /**< Short queue, or the persistent TX launch did not start; fill the TX FIFO. */
} uart_pio_tx_action_t;

/**
 * @brief Return whether a hardware UART is idle enough to apply line format.
 * @param tx_occupancy_nonzero Retained for a common backend call shape; queued
 * bytes may wait in the ring while the format changes.
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
    (void)tx_occupancy_nonzero;
    return !tx_dma_active && !uart_busy && !rx_fifo_readable;
}

/**
 * @brief Return whether a PIO UART is idle enough to start a baud-change pause.
 * @param tx_dma_active True while a TX DMA transfer is in flight.
 * @param tx_occupancy_nonzero Retained for a common backend call shape; queued
 * bytes may wait in the ring while the baud changes.
 * @param tx_fifo_empty True when the PIO TX FIFO is empty.
 * @param tx_shifter_idle True when TXSTALL has re-asserted after write-clear.
 * @param rx_fifo_empty True when the PIO RX FIFO is empty.
 * @param rx_line_idle Driver-provided result of the configured RX idle policy.
 * @return `true` when the worker may pause RX DMA and change baud.
 */
static inline bool uart_pio_baud_change_idle(bool tx_dma_active,
                                             bool tx_occupancy_nonzero,
                                             bool tx_fifo_empty,
                                             bool tx_shifter_idle,
                                             bool rx_fifo_empty,
                                             bool rx_line_idle)
{
    (void)tx_occupancy_nonzero;
    return !tx_dma_active && tx_fifo_empty && tx_shifter_idle &&
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

/** @brief Apply active-low RTS hysteresis to the current RX occupancy. */
static inline bool uart_rx_rts_should_assert(bool currently_asserted,
                                             size_t occupancy,
                                             size_t ring_size)
{
    size_t high_watermark = (ring_size * 3u) / 4u;
    size_t low_watermark = ring_size / 2u;

    if (currently_asserted && (occupancy >= high_watermark)) {
        return false;
    }
    if (!currently_asserted && (occupancy <= low_watermark)) {
        return true;
    }
    return currently_asserted;
}

/**
 * @brief Decide whether an acknowledged RX completion still needs an ISR re-arm.
 *
 * The poll fallback can acknowledge a sticky IRQ and restart the channel before
 * the ISR runs. Rechecking BUSY and TRANS_COUNT prevents that delayed ISR from
 * replacing the already-running transfer.
 */
static inline bool uart_rx_dma_irq_should_rearm(bool owner_present,
                                                bool irq_pending,
                                                bool channel_busy,
                                                uint32_t remaining)
{
    return owner_present && irq_pending && !channel_busy && (remaining == 0u);
}

/**
 * @brief Select the PIO TX drain path for one worker sweep.
 * @param tx_dma_active True when a TX DMA transfer still owns a ring span.
 * @param occupancy TX ring occupancy in bytes.
 * @param dma_threshold Occupancy that prefers DMA over FIFO polling.
 * @return The next TX action. @ref UART_PIO_TX_START_DMA falls back to FIFO
 * drain when the persistent TX channel cannot start a transfer. Channels are
 * claimed at init, not on each launch.
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
 * @brief Bound one TX launch by ring data, configured maximum, and wire time.
 * @param occupancy TX ring occupancy in bytes.
 * @param max_transfer Configured per-launch maximum.
 * @param baud_rate Active line rate in bits per second.
 * @param bits_per_frame Conservative number of wire bits consumed by each byte.
 * @param budget_ms Target maximum wire time for the launch.
 * @return Bytes to launch, with a one-frame floor when data is available.
 */
static inline size_t uart_tx_transfer_bytes(size_t occupancy,
                                            size_t max_transfer,
                                            uint32_t baud_rate,
                                            uint32_t bits_per_frame,
                                            uint32_t budget_ms)
{
    uint64_t budget_bits;
    size_t budget_bytes;
    size_t transfer_bytes = (max_transfer > occupancy) ? occupancy : max_transfer;

    if ((transfer_bytes == 0u) || (baud_rate == 0u) ||
        (bits_per_frame == 0u) || (budget_ms == 0u)) {
        return 0u;
    }

    budget_bits = (uint64_t)baud_rate * budget_ms;
    budget_bytes = (size_t)(budget_bits / (1000u * bits_per_frame));
    /* A frame can exceed the target at very low baud, but progress must continue. */
    if (budget_bytes == 0u) {
        budget_bytes = 1u;
    }

    return (transfer_bytes > budget_bytes) ? budget_bytes : transfer_bytes;
}

/**
 * @brief Return whether a DMA IRQ owner slot should ack and re-arm RX DMA.
 * @param owner_present True when this channel has a registered UART backend.
 * @param irq_pending True when this channel's bit is set in the IRQ status.
 * @return `true` when the ISR should acknowledge and reload TRANS_COUNT.
 */
static inline bool uart_dma_irq_should_service_owner(bool owner_present, bool irq_pending)
{
    return owner_present && irq_pending;
}

#endif
