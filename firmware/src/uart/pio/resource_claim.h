/**
 * @file resource_claim.h
 * @brief Injectable seam for PIO UART state-machine + DMA channel acquisition.
 *
 * @ref pio_uart_driver_claim_resources contains the exact claim-then-rollback
 * sequence used by @ref pio_uart_driver_init: claim the TX and RX state
 * machines, then an RX DMA channel, then a TX DMA channel, unclaiming
 * whatever was already claimed if any later step fails. It has no Pico SDK
 * includes (the PIO block is passed as an opaque pointer) so host unit tests
 * can link it directly and fault-inject claim failures through
 * @ref pio_uart_resource_claim_ops_t, without needing real hardware or a
 * stubbed SDK. Production always passes
 * @ref pio_uart_resource_claim_ops_default, which forwards to the real
 * `pio_sm_claim` / `pio_sm_unclaim` / `dma_claim_unused_channel` /
 * `dma_channel_unclaim` SDK calls with no behavior change.
 */

#ifndef PIO_UART_RESOURCE_CLAIM_H
#define PIO_UART_RESOURCE_CLAIM_H

#include <stdbool.h>

/**
 * @brief Resource-acquisition operations backing PIO SM and DMA claims.
 *
 * The production instance (@ref pio_uart_resource_claim_ops_default) binds
 * these to the real Pico SDK PIO/DMA claim functions. Host tests provide a
 * fake instance to simulate claim exhaustion. The PIO block is passed as an
 * opaque pointer so this header does not depend on hardware/pio.h.
 */
typedef struct {
    bool (*sm_is_claimed)(void *pio, unsigned int sm); /**< Mirrors `pio_sm_is_claimed`. */
    void (*sm_claim)(void *pio, unsigned int sm); /**< Mirrors `pio_sm_claim`. */
    void (*sm_unclaim)(void *pio, unsigned int sm); /**< Mirrors `pio_sm_unclaim`. */
    int (*claim_dma_channel)(bool required); /**< Mirrors `dma_claim_unused_channel`. */
    void (*unclaim_dma_channel)(unsigned int channel); /**< Mirrors `dma_channel_unclaim`. */
} pio_uart_resource_claim_ops_t;

/** @brief Production ops bound to the real Pico SDK PIO/DMA claim functions. */
extern const pio_uart_resource_claim_ops_t pio_uart_resource_claim_ops_default;

/**
 * @brief Claim the TX/RX state machines and RX/TX DMA channels, rolling back
 *        on partial failure.
 *
 * @param ops Claim/unclaim operations to use (production or test-injected).
 * @param pio PIO block owning @p tx_sm and @p rx_sm (opaque; forwarded to @p ops).
 * @param tx_sm TX state-machine index.
 * @param rx_sm RX state-machine index.
 * @param tx_sm_claimed Out: true once the TX state machine is claimed.
 * @param rx_sm_claimed Out: true once the RX state machine is claimed.
 * @param rx_dma_channel Out: claimed RX DMA channel, or -1 on failure.
 * @param tx_dma_channel Out: claimed TX DMA channel, or -1 on failure.
 * @return `true` when all four resources were claimed, `false` otherwise. On
 *         failure everything claimed during this call has been unclaimed and
 *         reset to its unclaimed value (false / -1).
 */
bool pio_uart_driver_claim_resources(const pio_uart_resource_claim_ops_t *ops,
                                     void *pio,
                                     unsigned int tx_sm,
                                     unsigned int rx_sm,
                                     bool *tx_sm_claimed,
                                     bool *rx_sm_claimed,
                                     int *rx_dma_channel,
                                     int *tx_dma_channel);

#endif
