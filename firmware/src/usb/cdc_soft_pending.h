/**
 * @file cdc_soft_pending.h
 * @brief Pure host-testable CDC line-state and soft-pending helpers.
 */

#ifndef USB_CDC_SOFT_PENDING_H
#define USB_CDC_SOFT_PENDING_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Decide whether a DTR close requests a fresh CDC/UART channel state.
 * @param was_open Previous DTR state for this interface.
 * @param is_open New DTR state for this interface.
 * @return `true` only on an open-to-closed transition.
 */
static inline bool usb_cdc_dtr_close_should_stop_channel(bool was_open, bool is_open)
{
    return was_open && !is_open;
}

/**
 * @brief Decide whether arming soft-pending should refresh the expiry deadline.
 * @param was_pending True if soft-pending was already armed before this request.
 * @param same_request True if the new line coding is identical to the pending one.
 * @return `false` only for an identical retry of an already-pending request.
 */
static inline bool usb_cdc_soft_pending_should_set_deadline(bool was_pending, bool same_request)
{
    return !was_pending || !same_request;
}

#endif
