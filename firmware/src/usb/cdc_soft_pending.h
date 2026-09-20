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
 * @param same_request True if the new line coding is identical to the pending one.
 * @return `false` only for an identical retry of an already-pending request.
 */
static inline bool usb_cdc_soft_pending_should_set_deadline(bool was_pending, bool same_request)
{
    return !was_pending || !same_request;
}

/**
 * @brief Decide whether an armed soft-pending request has genuinely timed out.
 * @param deadline_is_nil True if the recorded deadline was never armed, e.g. because a host
 *                        reset (tud_mount_cb/tud_umount_cb) cancelled the request.
 * @param deadline_reached True if the current time has reached the recorded deadline.
 * @return `true` only when a real deadline was armed and it has elapsed. A nil deadline means
 *         the request was cancelled, not that it timed out, so it must never report a timeout
 *         even if a caller mistakenly treats nil_time as "already reached".
 */
static inline bool usb_cdc_soft_pending_has_timed_out(bool deadline_is_nil, bool deadline_reached)
{
    return !deadline_is_nil && deadline_reached;
}

#endif
