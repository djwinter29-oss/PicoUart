/**
 * @file plane.c
 * @brief Deferred UART line-coding application and status ownership.
 */

#include "uart/control/plane.h"

#include "uart/control/ownership.h"
#include "uart/line_coding.h"
#include "uart/ring_buffer/ring_buffer.h"

#include "pico/time.h"

/** @brief Maximum time the worker may defer applying a line-coding change. */
#define UART_CONTROL_PLANE_APPLY_TIMEOUT_MS 1000u

static bool uart_control_plane_mailbox_has_pending_port(const uart_control_plane_t *control_plane,
                                                         uart_port_id_t port_id)
{
    return (control_plane != NULL) && (control_plane->mailboxes != NULL) &&
           (port_id < UART_PORT_COUNT) &&
           uart_control_mailbox_has_pending_port(&control_plane->mailboxes[port_id],
                                                 (uint32_t)port_id);
}

static void uart_control_plane_finish_worker_control(uart_control_plane_t *control_plane,
                                                     uart_port_id_t port_id,
                                                     uint32_t control_generation,
                                                     bool success)
{
    uint32_t save = spin_lock_blocking(control_plane->status_lock);

    control_plane->pending_controls[port_id].pending = false;
    if (uart_control_pending_should_clear(control_plane->soft_pending_controls[port_id],
                                          uart_control_plane_mailbox_has_pending_port(control_plane,
                                                                                      port_id))) {
        control_plane->status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    }
    uart_control_apply_completion_error(&control_plane->status_flags[port_id],
                                        UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                                        control_generation,
                                        control_plane->control_generations[port_id],
                                        success);
    spin_unlock(control_plane->status_lock, save);
}

static void uart_control_plane_finish_mailbox_control(uart_control_plane_t *control_plane,
                                                      uart_port_id_t port_id,
                                                      uint32_t control_generation,
                                                      bool success)
{
    uint32_t save = spin_lock_blocking(control_plane->status_lock);

    if (uart_control_pending_should_clear(
            control_plane->soft_pending_controls[port_id],
            uart_control_plane_mailbox_has_pending_port(control_plane, port_id))) {
        control_plane->status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    }
    uart_control_apply_completion_error(&control_plane->status_flags[port_id],
                                        UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                                        control_generation,
                                        control_plane->control_generations[port_id],
                                        success);
    spin_unlock(control_plane->status_lock, save);
}

static void uart_control_plane_set_worker_pending(uart_control_plane_t *control_plane,
                                                  uart_port_id_t port_id,
                                                  uint32_t control_generation)
{
    uint32_t save = spin_lock_blocking(control_plane->status_lock);

    control_plane->pending_controls[port_id].pending = true;
    control_plane->pending_controls[port_id].control_generation = control_generation;
    control_plane->status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    if (uart_control_completion_is_current(control_generation,
                                           control_plane->control_generations[port_id])) {
        control_plane->status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    }
    spin_unlock(control_plane->status_lock, save);
}

static bool uart_control_plane_tx_boundary_drained(uart_runtime_port_t *port,
                                                   uint32_t boundary_sequence)
{
    ring_buffer_t *tx_ring;

    if ((port == NULL) || (port->ops == NULL)) {
        return false;
    }

    tx_ring = port->ops->tx_ring(&port->backend);
    return (tx_ring != NULL) &&
           uart_control_tx_boundary_drained(tx_ring->consumer, boundary_sequence);
}

static void uart_control_plane_service_pending(uart_control_plane_t *control_plane,
                                               uart_port_id_t port_id)
{
    uart_runtime_port_t *port = &control_plane->ports[port_id];
    uart_control_pending_t *pending_control = &control_plane->pending_controls[port_id];
    bool applied;

    if (!pending_control->pending) {
        return;
    }

    if (!uart_control_plane_tx_boundary_drained(port, pending_control->tx_boundary_sequence)) {
        if (time_reached(pending_control->deadline)) {
            uart_control_plane_finish_worker_control(control_plane,
                                                      port_id,
                                                      pending_control->control_generation,
                                                      false);
        }
        return;
    }

    if (port->ops == NULL) {
        uart_control_plane_finish_worker_control(control_plane,
                                                  port_id,
                                                  pending_control->control_generation,
                                                  false);
        return;
    }

    applied = port->ops->set_line_coding(&port->backend, &pending_control->line_coding);
    if (!applied) {
        if (time_reached(pending_control->deadline)) {
            uart_control_plane_finish_worker_control(control_plane,
                                                      port_id,
                                                      pending_control->control_generation,
                                                      false);
        }
        return;
    }

    {
        uint32_t save = spin_lock_blocking(control_plane->status_lock);
        port->info.baud_rate = port->ops->baud_rate(&port->backend);
        spin_unlock(control_plane->status_lock, save);
    }
    uart_control_plane_finish_worker_control(control_plane,
                                              port_id,
                                              pending_control->control_generation,
                                              true);
}

static void uart_control_plane_set_line_coding(uart_control_plane_t *control_plane,
                                               const uart_control_mailbox_request_t *request)
{
    uart_port_id_t port_id = (uart_port_id_t)request->port_id;
    uart_runtime_port_t *port;
    uart_control_pending_t *pending_control;
    bool was_pending;
    bool same_request;

    /* The mailbox is private, but reject malformed future control requests
     * after acknowledging them so one bad payload cannot occupy the slot. */
    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    port = &control_plane->ports[port_id];
    pending_control = &control_plane->pending_controls[port_id];
    if (!uart_line_coding_is_valid(&request->line_coding)) {
        uart_control_plane_finish_mailbox_control(control_plane,
                                                  port_id,
                                                  request->control_generation,
                                                  false);
        return;
    }

    was_pending = pending_control->pending;
    same_request = was_pending &&
                   (pending_control->line_coding.baud_rate == request->line_coding.baud_rate) &&
                   (pending_control->line_coding.data_bits == request->line_coding.data_bits) &&
                   (pending_control->line_coding.stop_bits == request->line_coding.stop_bits) &&
                   (pending_control->line_coding.parity == request->line_coding.parity);

    if ((port->ops != NULL) &&
        port->ops->line_coding_matches(&port->backend, &request->line_coding)) {
        if (pending_control->pending) {
            uart_control_plane_finish_worker_control(control_plane,
                                                      port_id,
                                                      pending_control->control_generation,
                                                      true);
        }
        uart_control_plane_finish_mailbox_control(control_plane,
                                                   port_id,
                                                   request->control_generation,
                                                   true);
        return;
    }

    if ((port->ops == NULL) || !port->ops->line_coding_acceptable(&request->line_coding)) {
        uart_control_plane_finish_mailbox_control(control_plane,
                                                  port_id,
                                                  request->control_generation,
                                                  false);
        return;
    }

    pending_control->line_coding = request->line_coding;
    if (uart_control_worker_should_set_deadline(was_pending, same_request)) {
        pending_control->deadline = make_timeout_time_ms(UART_CONTROL_PLANE_APPLY_TIMEOUT_MS);
    }
    pending_control->tx_boundary_sequence = request->tx_boundary_sequence;
    uart_control_plane_set_worker_pending(control_plane, port_id, request->control_generation);
}

void uart_control_plane_service(uart_control_plane_t *control_plane)
{
    if ((control_plane == NULL) || (control_plane->ports == NULL) ||
        (control_plane->mailboxes == NULL) || (control_plane->pending_controls == NULL) ||
        (control_plane->soft_pending_controls == NULL) ||
        (control_plane->control_generations == NULL) || (control_plane->status_flags == NULL) ||
        (control_plane->status_lock == NULL) || (control_plane->stats_sequence == NULL) ||
        (control_plane->poll_start_index == NULL)) {
        return;
    }

    for (size_t offset = 0u; offset < UART_PORT_COUNT; ++offset) {
        size_t index = (*control_plane->poll_start_index + offset) % UART_PORT_COUNT;
        uart_control_mailbox_request_t request;

        if (uart_control_mailbox_take(&control_plane->mailboxes[index], &request)) {
            uart_control_plane_set_line_coding(control_plane, &request);
        }
    }

    for (size_t offset = 0u; offset < UART_PORT_COUNT; ++offset) {
        size_t index = (*control_plane->poll_start_index + offset) % UART_PORT_COUNT;

        control_plane->stats_sequence[index] += 1u;
        __dmb();
        uart_control_plane_service_pending(control_plane, (uart_port_id_t)index);
        __dmb();
        control_plane->stats_sequence[index] += 1u;
    }

    *control_plane->poll_start_index = (*control_plane->poll_start_index + 1u) % UART_PORT_COUNT;
}

bool uart_control_plane_tx_launch_allowed(const uart_control_plane_t *control_plane,
                                          uart_port_id_t port_id)
{
    if ((control_plane == NULL) || (port_id >= UART_PORT_COUNT)) {
        return false;
    }

    return !control_plane->pending_controls[port_id].pending ||
           !uart_control_plane_tx_boundary_drained(&control_plane->ports[port_id],
                                                   control_plane->pending_controls[port_id]
                                                       .tx_boundary_sequence);
}