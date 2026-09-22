/**
 * @file control_mailbox.h
 * @brief Single-producer/single-consumer UART control mailbox.
 */

#ifndef UART_CONTROL_MAILBOX_H
#define UART_CONTROL_MAILBOX_H

#include "uart/uart_driver.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief One line-coding request transferred from USB core to UART worker core.
 */
typedef struct {
    uint32_t port_id; /**< Logical UART port identifier. */
    uint32_t control_generation; /**< Host control-request generation. */
    uint32_t tx_boundary_sequence; /**< Last TX byte admitted before ingress paused. */
    uart_driver_line_coding_t line_coding; /**< Requested UART line format. */
} uart_control_mailbox_request_t;

/**
 * @brief Single-slot mailbox shared by core 0 producer and core 1 consumer.
 */
typedef struct {
    volatile uint32_t request_sequence; /**< Monotonic sequence published by core 0. */
    volatile uint32_t response_sequence; /**< Latest sequence completed by core 1. */
    volatile uart_control_mailbox_request_t request; /**< Payload associated with request_sequence. */
} uart_control_mailbox_t;

/**
 * @brief Reset an empty mailbox before the worker starts.
 * @param mailbox Mailbox to reset.
 */
void uart_control_mailbox_reset(uart_control_mailbox_t *mailbox);

/**
 * @brief Return whether the mailbox can accept one request.
 * @param mailbox Mailbox to inspect.
 * @return `true` when the single request slot is empty.
 */
bool uart_control_mailbox_can_publish(const uart_control_mailbox_t *mailbox);

/**
 * @brief Publish a request when the mailbox is empty.
 * @param mailbox Mailbox owned by the USB-core producer.
 * @param request Request payload to publish.
 * @return `true` when published, or `false` when the slot remains occupied.
 */
bool uart_control_mailbox_publish(uart_control_mailbox_t *mailbox,
                                  const uart_control_mailbox_request_t *request);

/**
 * @brief Consume the currently published request, if any.
 * @param mailbox Mailbox owned by the UART-worker consumer.
 * @param request Output storage for the request payload.
 * @return `true` when @p request was populated and acknowledged.
 */
bool uart_control_mailbox_take(uart_control_mailbox_t *mailbox,
                               uart_control_mailbox_request_t *request);

/**
 * @brief Return whether the pending request belongs to one logical port.
 * @param mailbox Mailbox to inspect.
 * @param port_id Logical UART port identifier.
 * @return `true` while a request for @p port_id awaits worker consumption.
 */
bool uart_control_mailbox_has_pending_port(const uart_control_mailbox_t *mailbox,
                                           uint32_t port_id);

#endif