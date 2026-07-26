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

/**
 * @brief Decide whether an invalid new SET_LINE_CODING may drop prior soft-pending.
 * @param soft_pending_armed True when a prior valid request is still soft-pending.
 * @return `true` when the prior request must be preserved.
 */
static inline bool usb_cdc_soft_pending_preserve_on_reject(bool soft_pending_armed)
{
    return soft_pending_armed;
}

/**
 * @brief Decide whether soft-pending timeout may clear CONTROL_PENDING.
 * @param worker_has_deferred True when the UART worker still owns a deferred apply.
 * @return `true` when CDC owned the only in-flight control request.
 */
static inline bool usb_cdc_soft_pending_timeout_clears_pending(bool worker_has_deferred)
{
    return !worker_has_deferred;
}

#endif
