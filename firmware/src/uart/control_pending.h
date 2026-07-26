/**
 * @file control_pending.h
 * @brief Pure ownership rule for a UART control-pending status flag.
 */

#ifndef UART_CONTROL_PENDING_H
#define UART_CONTROL_PENDING_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Decide whether a worker completion may clear CONTROL_PENDING.
 * @param soft_pending True while core 0 has a request waiting for the mailbox.
 * @param mailbox_pending True while the mailbox contains a request for this port.
 * @return `true` when no newer control request owns the pending status.
 */
static inline bool uart_control_pending_should_clear(bool soft_pending, bool mailbox_pending)
{
    return !soft_pending && !mailbox_pending;
}

/**
 * @brief Decide whether a control completion may update the latest error status.
 * @param completion_generation Generation carried by the completing request.
 * @param latest_generation Most recent host control-request generation.
 * @return `true` only when the completing request is still the newest request.
 */
static inline bool uart_control_completion_is_current(uint32_t completion_generation,
                                                      uint32_t latest_generation)
{
    return completion_generation == latest_generation;
}

#endif