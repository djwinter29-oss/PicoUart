/**
 * @file ownership.h
 * @brief Pure ownership rule for a UART control-pending status flag.
 */

#ifndef UART_CONTROL_OWNERSHIP_H
#define UART_CONTROL_OWNERSHIP_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Return whether one mailbox slot can accept a new request.
 * @param request_sequence Sequence most recently published by the producer.
 * @param response_sequence Sequence most recently completed by the worker.
 * @return `true` when the mailbox slot is empty.
 */
static inline bool uart_control_mailbox_is_empty(uint32_t request_sequence,
                                                  uint32_t response_sequence)
{
    return request_sequence == response_sequence;
}

/**
 * @brief Advance a mailbox request sequence with defined unsigned wraparound.
 * @param request_sequence Current producer sequence.
 * @return Next producer sequence.
 */
static inline uint32_t uart_control_mailbox_next_sequence(uint32_t request_sequence)
{
    return request_sequence + 1u;
}

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
 * @brief Decide whether UART TX ingress must wait for a control transition.
 * @param status_flags Lock-protected status flags for one logical UART port.
 * @param control_pending_bit Status bit representing continuous control ownership.
 * @return `true` while the UART format may be changing or is about to change.
 *
 * CONTROL_PENDING spans the soft-pending, mailbox-pending, and worker-pending
 * phases. Using that continuous flag avoids admitting TX in the handoff between
 * mailbox acknowledgement and worker-pending ownership.
 */
static inline bool uart_control_tx_should_block(uint8_t status_flags,
                                                uint8_t control_pending_bit)
{
    return (status_flags & control_pending_bit) != 0u;
}

/**
 * @brief Return whether all TX bytes admitted before a control boundary drained.
 * @param consumer_sequence Current TX consumer sequence.
 * @param boundary_sequence Last TX producer sequence admitted before the change.
 * @return `true` when the old-format TX boundary has drained.
 */
static inline bool uart_control_tx_boundary_drained(uint32_t consumer_sequence,
                                                    uint32_t boundary_sequence)
{
    return consumer_sequence == boundary_sequence;
}

/**
 * @brief Decide whether a worker-side control request needs a new deadline.
 * @param was_pending True when the worker already owns a deferred request.
 * @param same_request True when the replacement requests the same format.
 * @return `false` only for an identical retry of an existing request.
 */
static inline bool uart_control_worker_should_set_deadline(bool was_pending, bool same_request)
{
    return !was_pending || !same_request;
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

/**
 * @brief Apply the error result of a control completion when it is current.
 * @param status_flags Port status flags word.
 * @param control_error_bit Status bit to set or clear.
 * @param completion_generation Generation carried by the completing request.
 * @param latest_generation Most recent host control-request generation.
 * @param success True when the completing request applied successfully.
 *
 * A stale completion cannot change the error state reported for a newer request.
 */
static inline void uart_control_apply_completion_error(volatile uint8_t *status_flags,
                                                       uint8_t control_error_bit,
                                                       uint32_t completion_generation,
                                                       uint32_t latest_generation,
                                                       bool success)
{
    if ((status_flags == NULL) ||
        !uart_control_completion_is_current(completion_generation, latest_generation)) {
        return;
    }

    if (success) {
        *status_flags &= (uint8_t)~control_error_bit;
    } else {
        *status_flags |= control_error_bit;
    }
}

/**
 * @brief Apply CONTROL_ERROR bookkeeping for a host reject.
 * @param generation Host control generation.
 * @param status_flags Port status flags word.
 * @param control_error_bit Status bit to set.
 *
 */
static inline void uart_control_apply_reject_error(uint32_t *generation,
                                                   volatile uint8_t *status_flags,
                                                   uint8_t control_error_bit)
{
    if ((generation == NULL) || (status_flags == NULL)) {
        return;
    }

    *generation += 1u;
    *status_flags |= control_error_bit;
}

#endif
