/**
 * @file cdc_soft_pending.h
 * @brief Pure helpers for CDC soft-pending deadline coalescing (host-testable).
 */

#ifndef USB_CDC_SOFT_PENDING_H
#define USB_CDC_SOFT_PENDING_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Decide whether arming soft-pending should refresh the expiry deadline.
 * @param was_pending True if soft-pending was already armed before this request.
 * @return `true` only on the transition into soft-pending (first arm).
 */
static inline bool usb_cdc_soft_pending_should_set_deadline(bool was_pending)
{
    return !was_pending;
}

#endif
