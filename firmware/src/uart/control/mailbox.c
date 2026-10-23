/**
 * @file control_mailbox.c
 * @brief Status-lock-owned UART control mailbox.
 */

#include "uart/control/mailbox.h"

void uart_control_mailbox_reset(uart_control_mailbox_t *mailbox)
{
    if (mailbox == NULL) {
        return;
    }

    *mailbox = (uart_control_mailbox_t){0};
}

bool uart_control_mailbox_can_publish(const uart_control_mailbox_t *mailbox)
{
    return (mailbox != NULL) && !mailbox->occupied;
}

bool uart_control_mailbox_publish(uart_control_mailbox_t *mailbox, const uart_control_mailbox_request_t *request)
{
    if ((request == NULL) || !uart_control_mailbox_can_publish(mailbox)) {
        return false;
    }

    mailbox->request = *request;
    mailbox->occupied = true;
    return true;
}

bool uart_control_mailbox_take(uart_control_mailbox_t *mailbox, uart_control_mailbox_request_t *request)
{
    if ((mailbox == NULL) || (request == NULL) || !mailbox->occupied) {
        return false;
    }

    *request = mailbox->request;
    mailbox->occupied = false;
    return true;
}

bool uart_control_mailbox_has_pending_port(const uart_control_mailbox_t *mailbox, uint32_t port_id)
{
    return (mailbox != NULL) && mailbox->occupied && (mailbox->request.port_id == port_id);
}