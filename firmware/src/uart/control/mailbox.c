/**
 * @file control_mailbox.c
 * @brief Single-producer/single-consumer UART control mailbox.
 */

#include "uart/control/mailbox.h"

#include "hardware/sync.h"
#include "uart/control/ownership.h"

void uart_control_mailbox_reset(uart_control_mailbox_t *mailbox)
{
    if (mailbox == NULL) {
        return;
    }

    mailbox->request_sequence = 0u;
    mailbox->response_sequence = 0u;
    mailbox->request.port_id = 0u;
    mailbox->request.control_generation = 0u;
    mailbox->request.tx_boundary_sequence = 0u;
    mailbox->request.line_coding.baud_rate = 0u;
    mailbox->request.line_coding.data_bits = 8u;
    mailbox->request.line_coding.stop_bits = 1u;
    mailbox->request.line_coding.parity = UART_DRIVER_PARITY_NONE;
}

bool uart_control_mailbox_can_publish(const uart_control_mailbox_t *mailbox)
{
    return (mailbox != NULL) &&
           uart_control_mailbox_is_empty(mailbox->request_sequence, mailbox->response_sequence);
}

bool uart_control_mailbox_publish(uart_control_mailbox_t *mailbox,
                                  const uart_control_mailbox_request_t *request)
{
    uint32_t request_sequence;

    if ((request == NULL) || !uart_control_mailbox_can_publish(mailbox)) {
        return false;
    }

    request_sequence = uart_control_mailbox_next_sequence(mailbox->request_sequence);
    mailbox->request = *request;
    __dmb();
    mailbox->request_sequence = request_sequence;
    return true;
}

bool uart_control_mailbox_take(uart_control_mailbox_t *mailbox,
                               uart_control_mailbox_request_t *request)
{
    uint32_t request_sequence;

    if ((mailbox == NULL) || (request == NULL)) {
        return false;
    }

    request_sequence = mailbox->request_sequence;
    if (request_sequence == mailbox->response_sequence) {
        return false;
    }

    __dmb();
    *request = mailbox->request;
    __dmb();
    mailbox->response_sequence = request_sequence;
    return true;
}

bool uart_control_mailbox_has_pending_port(const uart_control_mailbox_t *mailbox,
                                           uint32_t port_id)
{
    uint32_t request_sequence;

    if (mailbox == NULL) {
        return false;
    }

    request_sequence = mailbox->request_sequence;
    if (request_sequence == mailbox->response_sequence) {
        return false;
    }

    __dmb();
    return mailbox->request.port_id == port_id;
}