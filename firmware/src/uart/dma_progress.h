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

#include "hardware/dma.h"
#include "hardware/structs/dma.h"

#include <stdint.h>

/**
 * @brief Encoded TRANS_COUNT used to arm a long-running RX DMA countdown.
 * @return RP2040: 0xffffffff. RP2350: normal-mode max COUNT (0x0fffffff).
 */
static inline uint32_t uart_dma_rx_transfer_count_encoded(void)
{
#if PICO_RP2040
    return 0xffffffffu;
#else
    return dma_encode_transfer_count(DMA_CH0_TRANS_COUNT_COUNT_BITS);
#endif
}

/**
 * @brief Maximum countdown value matching @ref uart_dma_rx_transfer_count_encoded.
 */
static inline uint32_t uart_dma_rx_transfer_count_max(void)
{
#if PICO_RP2040
    return 0xffffffffu;
#else
    return DMA_CH0_TRANS_COUNT_COUNT_BITS;
#endif
}

/**
 * @brief Live transfer-count field for progress math (MODE bits cleared on RP2350).
 * @param channel Claimed DMA channel index.
 */
static inline uint32_t uart_dma_rx_transfer_count_remaining(uint channel)
{
    uint32_t remaining = dma_hw->ch[channel].transfer_count;
#if !PICO_RP2040
    remaining &= DMA_CH0_TRANS_COUNT_COUNT_BITS;
#endif
    return remaining;
}

/**
 * @brief Bytes transferred since the channel was last armed with a full countdown.
 * @param channel Claimed DMA channel index.
 */
static inline uint32_t uart_dma_rx_progress(uint channel)
{
    return uart_dma_rx_transfer_count_max() - uart_dma_rx_transfer_count_remaining(channel);
}

#endif
