/**
 * @file resource_claim_ops.c
 * @brief Production PIO/DMA claim ops binding for @ref resource_claim.h.
 *
 * Kept separate from resource_claim.c so the claim/rollback algorithm stays
 * free of Pico SDK includes and is directly host-testable; only this
 * translation unit requires the real hardware/pio.h and hardware/dma.h. The
 * thin wrappers below exist solely to adapt the opaque `void *pio` seam
 * parameter to the real `PIO` type expected by the SDK.
 */

#include "uart/pio/resource_claim.h"

#include "hardware/dma.h"
#include "hardware/pio.h"

static bool pio_uart_resource_claim_sm_is_claimed(void *pio, unsigned int sm)
{
    return pio_sm_is_claimed((PIO)pio, sm);
}

static void pio_uart_resource_claim_sm_claim(void *pio, unsigned int sm)
{
    pio_sm_claim((PIO)pio, sm);
}

static void pio_uart_resource_claim_sm_unclaim(void *pio, unsigned int sm)
{
    pio_sm_unclaim((PIO)pio, sm);
}

const pio_uart_resource_claim_ops_t pio_uart_resource_claim_ops_default = {
    .sm_is_claimed = pio_uart_resource_claim_sm_is_claimed,
    .sm_claim = pio_uart_resource_claim_sm_claim,
    .sm_unclaim = pio_uart_resource_claim_sm_unclaim,
    .claim_dma_channel = dma_claim_unused_channel,
    .unclaim_dma_channel = dma_channel_unclaim,
};
