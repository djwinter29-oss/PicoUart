/**
 * @file dma_progress.h
 * @brief Shared RX DMA transfer-count helpers for hardware and PIO UART backends.
 *
 * RP2040 TRANS_COUNT is a full 32-bit countdown. RP2350 reuses the top nibble as
 * MODE: writing 0xffffffff selects ENDLESS mode (count never decrements, no IRQ).
 * Always encode a normal countdown via the SDK helper and mask COUNT when sampling
 * progress.
 */

#ifndef UART_DMA_PROGRESS_H
#define UART_DMA_PROGRESS_H

#include "uart/dma_progress_math.h"

#include "hardware/dma.h"
#include "hardware/structs/dma.h"
#include "pico/time.h"

#include <stdint.h>

/**
 * @brief Encoded TRANS_COUNT used to arm a long-running RX DMA countdown.
 * @return RP2040: 0xffffffff. RP2350: normal-mode max COUNT (0x0fffffff).
 */
static inline uint32_t uart_dma_rx_transfer_count_encoded(void)
{
#if PICO_RP2040
    return UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040;
#else
    return dma_encode_transfer_count(UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350);
#endif
}

/**
 * @brief Maximum countdown value matching @ref uart_dma_rx_transfer_count_encoded.
 */
static inline uint32_t uart_dma_rx_transfer_count_max(void)
{
#if PICO_RP2040
    return UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040;
#else
    return UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350;
#endif
}

/**
 * @brief Live transfer-count field for progress math (MODE bits cleared on RP2350).
 * @param channel Claimed DMA channel index.
 */
static inline uint32_t uart_dma_rx_transfer_count_remaining(uint channel)
{
    uint32_t remaining = dma_hw->ch[channel].transfer_count;
#if PICO_RP2040
    remaining = uart_dma_rx_mask_remaining(remaining, UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040);
#else
    remaining = uart_dma_rx_mask_remaining(remaining, UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350);
#endif
    return remaining;
}

/**
 * @brief Bytes transferred since the channel was last armed with a full countdown.
 * @param channel Claimed DMA channel index.
 */
static inline uint32_t uart_dma_rx_progress(uint channel)
{
    return uart_dma_rx_progress_from_remaining(uart_dma_rx_transfer_count_remaining(channel),
                                               uart_dma_rx_transfer_count_max());
}

/**
 * @brief After clearing DMA CH EN, wait until TRANS_COUNT stops changing.
 * @param channel Claimed RX DMA channel (already paused: EN cleared).
 *
 * Paused channels keep BUSY high until CHAN_ABORT, so callers must not spin on
 * BUSY. An in-flight beat may still retire and decrement TRANS_COUNT; wait with
 * a real-time floor plus consecutive identical samples before publish-before-abort.
 * Publish must still happen before abort: abort does not promise a usable
 * TRANS_COUNT on every target. On timeout, take one final grace sample so the
 * caller still publishes the best available count.
 *
 * Call with global IRQs enabled when possible: per-channel IRQ masking already
 * stops this channel's re-arm; settling under CPSID stalls sibling port IRQs.
 */
static inline void uart_dma_rx_wait_paused_progress_stable(uint channel)
{
    uint32_t last;
    uint32_t stable = 0u;
    absolute_time_t deadline;

    busy_wait_us_32(UART_DMA_RX_PAUSE_SETTLE_FLOOR_US);

    last = uart_dma_rx_transfer_count_remaining(channel);
    deadline = make_timeout_time_us(UART_DMA_RX_PAUSE_SETTLE_TIMEOUT_US);

    while (!time_reached(deadline)) {
        tight_loop_contents();
        if (uart_dma_rx_paused_progress_sample(uart_dma_rx_transfer_count_remaining(channel),
                                               &last,
                                               &stable,
                                               UART_DMA_RX_PAUSE_STABLE_SAMPLES)) {
            return;
        }
    }

    busy_wait_us_32(UART_DMA_RX_PAUSE_SETTLE_GRACE_US);
}

#endif
