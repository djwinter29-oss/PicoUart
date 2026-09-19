/**
 * @file dma_claim_ops.c
 * @brief Production DMA claim ops binding for @ref dma_claim.h.
 *
 * Kept separate from dma_claim.c so the claim/rollback algorithm stays free
 * of Pico SDK includes and is directly host-testable; only this translation
 * unit requires the real hardware/dma.h.
 */

#include "uart/hw/dma_claim.h"

#include "hardware/dma.h"

const hw_uart_dma_claim_ops_t hw_uart_driver_dma_claim_ops_default = {
    .claim_channel = dma_claim_unused_channel,
    .unclaim_channel = dma_channel_unclaim,
};
