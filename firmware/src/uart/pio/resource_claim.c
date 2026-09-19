/**
 * @file resource_claim.c
 * @brief PIO UART SM/DMA claim/rollback logic (see @ref resource_claim.h).
 */

#include "uart/pio/resource_claim.h"

#include <stddef.h>

bool pio_uart_driver_claim_resources(const pio_uart_resource_claim_ops_t *ops,
                                     void *pio,
                                     unsigned int tx_sm,
                                     unsigned int rx_sm,
                                     bool *tx_sm_claimed,
                                     bool *rx_sm_claimed,
                                     int *rx_dma_channel,
                                     int *tx_dma_channel)
{
    if ((ops == NULL) || (tx_sm_claimed == NULL) || (rx_sm_claimed == NULL) ||
        (rx_dma_channel == NULL) || (tx_dma_channel == NULL)) {
        return false;
    }

    *tx_sm_claimed = false;
    *rx_sm_claimed = false;
    *rx_dma_channel = -1;
    *tx_dma_channel = -1;

    if (ops->sm_is_claimed(pio, tx_sm) || ops->sm_is_claimed(pio, rx_sm)) {
        return false;
    }

    ops->sm_claim(pio, tx_sm);
    *tx_sm_claimed = true;
    ops->sm_claim(pio, rx_sm);
    *rx_sm_claimed = true;

    *rx_dma_channel = ops->claim_dma_channel(false);
    if (*rx_dma_channel < 0) {
        goto unclaim_state_machines;
    }

    *tx_dma_channel = ops->claim_dma_channel(false);
    if (*tx_dma_channel < 0) {
        ops->unclaim_dma_channel((unsigned int)*rx_dma_channel);
        *rx_dma_channel = -1;
        goto unclaim_state_machines;
    }

    return true;

unclaim_state_machines:
    if (*rx_sm_claimed) {
        ops->sm_unclaim(pio, rx_sm);
        *rx_sm_claimed = false;
    }
    if (*tx_sm_claimed) {
        ops->sm_unclaim(pio, tx_sm);
        *tx_sm_claimed = false;
    }
    return false;
}
