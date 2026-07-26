/**
 * @file dma_progress_math.h
 * @brief Pure RX DMA progress helpers shared by firmware and host unit tests.
 *
 * Hardware register access lives in @ref dma_progress.h. This header stays free
 * of Pico SDK includes so Unity host tests can lock the wrap/mask arithmetic.
 */

#ifndef UART_DMA_PROGRESS_MATH_H
#define UART_DMA_PROGRESS_MATH_H

#include <stdint.h>

/** @brief Full 32-bit TRANS_COUNT countdown used on RP2040. */
#define UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040 0xffffffffu
/**
 * @brief RP2350 TRANS_COUNT COUNT field mask / max (MODE bits excluded).
 *
 * Matches `DMA_CH0_TRANS_COUNT_COUNT_BITS` in the Pico SDK hardware headers.
 */
#define UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350 0x0fffffffu

/**
 * @brief Bytes produced between two progress samples of one countdown.
 * @param progress Current progress (`max - remaining`).
 * @param last_progress Previous published progress sample.
 * @param transfer_count_max Countdown max used when the channel was armed.
 * @return Bytes advanced since @p last_progress, including a wrap through max.
 */
static inline uint32_t uart_dma_rx_bytes_produced(uint32_t progress,
                                                 uint32_t last_progress,
                                                 uint32_t transfer_count_max)
{
    if (progress < last_progress) {
        return (transfer_count_max - last_progress) + progress;
    }

    return progress - last_progress;
}

/**
 * @brief Mask a raw TRANS_COUNT register value down to the COUNT field.
 * @param raw_transfer_count Value read from DMA `transfer_count`.
 * @param count_mask Field mask (`0xffffffff` on RP2040, `0x0fffffff` on RP2350).
 */
static inline uint32_t uart_dma_rx_mask_remaining(uint32_t raw_transfer_count,
                                                 uint32_t count_mask)
{
    return raw_transfer_count & count_mask;
}

/**
 * @brief Convert a remaining COUNT value into progress since arming.
 * @param remaining Masked remaining transfer count.
 * @param transfer_count_max Countdown max used when the channel was armed.
 */
static inline uint32_t uart_dma_rx_progress_from_remaining(uint32_t remaining,
                                                          uint32_t transfer_count_max)
{
    return transfer_count_max - remaining;
}

#endif
