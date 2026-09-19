/**
 * @file dma_claim.h
 * @brief Injectable seam for HW UART RX/TX DMA channel acquisition.
 *
 * @ref hw_uart_driver_claim_dma_channels contains the exact claim-then-
 * rollback sequence used by @ref hw_uart_driver_init: claim an RX channel,
 * then a TX channel, and unclaim RX again if the TX claim fails. It has no
 * Pico SDK includes so host unit tests can link it directly and fault-inject
 * claim failures through @ref hw_uart_dma_claim_ops_t, without needing real
 * hardware or a stubbed SDK. Production always passes
 * @ref hw_uart_driver_dma_claim_ops_default, which forwards to the real
 * `dma_claim_unused_channel` / `dma_channel_unclaim` SDK calls with no
 * behavior change.
 */

#ifndef HW_UART_DMA_CLAIM_H
#define HW_UART_DMA_CLAIM_H

#include <stdbool.h>

/**
 * @brief Resource-acquisition operations backing one DMA channel claim/release.
 *
 * The production instance (@ref hw_uart_driver_dma_claim_ops_default) binds
 * these to the real Pico SDK `dma_claim_unused_channel` / `dma_channel_unclaim`
 * functions. Host tests provide a fake instance to simulate claim exhaustion.
 */
typedef struct {
    int (*claim_channel)(bool required); /**< Mirrors `dma_claim_unused_channel`. */
    void (*unclaim_channel)(unsigned int channel); /**< Mirrors `dma_channel_unclaim`. */
} hw_uart_dma_claim_ops_t;

/** @brief Production ops bound to the real Pico SDK DMA claim functions. */
extern const hw_uart_dma_claim_ops_t hw_uart_driver_dma_claim_ops_default;

/**
 * @brief Claim an RX and TX DMA channel pair, rolling back on partial failure.
 *
 * @param ops Claim/unclaim operations to use (production or test-injected).
 * @param rx_dma_channel Out: claimed RX channel, or -1 on failure.
 * @param tx_dma_channel Out: claimed TX channel, or -1 on failure.
 * @return `true` when both channels were claimed, `false` otherwise. On
 *         failure any channel claimed during this call has been unclaimed
 *         and both outputs are reset to -1.
 */
bool hw_uart_driver_claim_dma_channels(const hw_uart_dma_claim_ops_t *ops,
                                      int *rx_dma_channel,
                                      int *tx_dma_channel);

#endif
