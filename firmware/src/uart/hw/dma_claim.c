/**
 * @file dma_claim.c
 * @brief HW UART DMA channel claim/rollback logic (see @ref dma_claim.h).
 */

#include "uart/hw/dma_claim.h"

#include <stddef.h>

bool hw_uart_driver_claim_dma_channels(const hw_uart_dma_claim_ops_t *ops,
                                      int *rx_dma_channel,
                                      int *tx_dma_channel)
{
    if ((ops == NULL) || (rx_dma_channel == NULL) || (tx_dma_channel == NULL)) {
        return false;
    }

    *rx_dma_channel = -1;
    *tx_dma_channel = -1;

    *rx_dma_channel = ops->claim_channel(false);
    if (*rx_dma_channel < 0) {
        return false;
    }

    *tx_dma_channel = ops->claim_channel(false);
    if (*tx_dma_channel < 0) {
        ops->unclaim_channel((unsigned int)*rx_dma_channel);
        *rx_dma_channel = -1;
        return false;
    }

    return true;
}
