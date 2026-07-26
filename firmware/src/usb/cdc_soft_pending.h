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
 * @brief True when a CDC line-coding reject should bump the control generation.
 *
 * Preserve-path rejects (prior soft-pending still armed) must only set
 * `CONTROL_ERROR` without bumping generation, so the armed request remains
 * current for a later successful apply. Status may briefly show
 * `CONTROL_ERROR | CONTROL_PENDING` until that apply completes.
 */
static inline bool usb_cdc_reject_should_bump_generation(bool soft_pending_armed)
{
    return !usb_cdc_soft_pending_preserve_on_reject(soft_pending_armed);
}

#endif
