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
                                                                                      port_id),
                                          false)) {
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
            uart_control_plane_mailbox_has_pending_port(control_plane, port_id),
            control_plane->pending_controls[port_id].pending)) {
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

    /* The apply deadline must be checked before the backend is touched: an
     * apply that races past an already-expired deadline must not be able to
     * report success and clear CONTROL_ERROR. */
    if (time_reached(pending_control->deadline)) {
        uart_control_plane_finish_worker_control(control_plane,
                                                  port_id,
                                                  pending_control->control_generation,
                                                  false);
        return;
    }

    applied = port->ops->set_line_coding(&port->backend, &pending_control->line_coding);
    if (!applied) {
        bool backend_stopped = (port->ops->is_initialized != NULL) &&
                               !port->ops->is_initialized(&port->backend);

        if (backend_stopped) {
            uint32_t save = spin_lock_blocking(control_plane->status_lock);

            control_plane->status_flags[port_id] |= UART_DRIVER_PORT_STATUS_INIT_FAILED;
            control_plane->status_flags[port_id] &=
                (uint8_t)~UART_DRIVER_PORT_STATUS_READY;
            spin_unlock(control_plane->status_lock, save);
            uart_control_plane_finish_worker_control(control_plane,
                                                      port_id,
                                                      pending_control->control_generation,
                                                      false);
            return;
        }

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

/**
 * @brief Release a provisional worker-ownership marker that never became a
 *        real deferred apply.
 * @param control_plane Private runtime state.
 * @param port_id Logical UART port whose provisional marker is released.
 *
 * @ref uart_control_plane_service sets `pending_controls[port_id].pending`
 * true in the same locked step as the mailbox acknowledgement so no
 * concurrent status read can ever see the request as owned by neither the
 * mailbox nor the worker (see uart_control_pending_should_clear()). Once the
 * request turns out not to need a deferred apply, that marker must be undone
 * under the same lock before the mailbox completion runs.
 */
static void uart_control_plane_release_provisional_pending(uart_control_plane_t *control_plane,
                                                            uart_port_id_t port_id)
{
    uint32_t save = spin_lock_blocking(control_plane->status_lock);
    control_plane->pending_controls[port_id].pending = false;
    spin_unlock(control_plane->status_lock, save);
}

static void uart_control_plane_set_line_coding(uart_control_plane_t *control_plane,
                                               const uart_control_mailbox_request_t *request,
                                               bool prior_pending)
{
    uart_port_id_t port_id = (uart_port_id_t)request->port_id;
    uart_runtime_port_t *port;
    uart_control_pending_t *pending_control;
    bool was_pending = prior_pending;
    bool same_request;

    /* The service loop rejects a payload whose port_id does not match its slot
     * before calling here, and clears that slot's pending state. */
    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    port = &control_plane->ports[port_id];
    pending_control = &control_plane->pending_controls[port_id];
    if (!uart_line_coding_is_valid(&request->line_coding)) {
        if (!prior_pending) {
            uart_control_plane_release_provisional_pending(control_plane, port_id);
        }
        uart_control_plane_finish_mailbox_control(control_plane,
                                                  port_id,
                                                  request->control_generation,
                                                  false);
        return;
    }

    same_request = was_pending &&
                   (pending_control->line_coding.baud_rate == request->line_coding.baud_rate) &&
                   (pending_control->line_coding.data_bits == request->line_coding.data_bits) &&
                   (pending_control->line_coding.stop_bits == request->line_coding.stop_bits) &&
                   (pending_control->line_coding.parity == request->line_coding.parity);

    if ((port->ops != NULL) &&
        port->ops->line_coding_matches(&port->backend, &request->line_coding)) {
        if (was_pending) {
            uart_control_plane_finish_worker_control(control_plane,
                                                      port_id,
                                                      pending_control->control_generation,
                                                      true);
        } else {
            uart_control_plane_release_provisional_pending(control_plane, port_id);
        }
        uart_control_plane_finish_mailbox_control(control_plane,
                                                   port_id,
                                                   request->control_generation,
                                                   true);
        return;
    }

    if ((port->ops == NULL) || !port->ops->line_coding_acceptable(&request->line_coding)) {
        if (!prior_pending) {
            uart_control_plane_release_provisional_pending(control_plane, port_id);
        }
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
        bool taken;
        bool prior_pending;
        uint32_t save = spin_lock_blocking(control_plane->status_lock);

        taken = uart_control_mailbox_take(&control_plane->mailboxes[index], &request);
        prior_pending = taken && control_plane->pending_controls[index].pending;
        if (taken && !prior_pending) {
            /* Register worker ownership in the same locked step as the
             * mailbox acknowledgement. Without this, a concurrent status
             * check (e.g. a soft-pending reject on core 0) could observe the
             * request as acknowledged by the mailbox but not yet owned by
             * the worker, and wrongly clear CONTROL_PENDING mid-handoff. */
            control_plane->pending_controls[index].pending = true;
            control_plane->pending_controls[index].control_generation = request.control_generation;
        }
        spin_unlock(control_plane->status_lock, save);

        if (!taken) {
            continue;
        }

        if (request.port_id != (uint32_t)index) {
            if (!prior_pending) {
                uart_control_plane_release_provisional_pending(control_plane,
                                                                (uart_port_id_t)index);
            }
            uart_control_plane_finish_mailbox_control(control_plane,
                                                      (uart_port_id_t)index,
                                                      request.control_generation,
                                                      false);
            continue;
        }

        uart_control_plane_set_line_coding(control_plane, &request, prior_pending);
    }

    for (size_t offset = 0u; offset < UART_PORT_COUNT; ++offset) {
        size_t index = (*control_plane->poll_start_index + offset) % UART_PORT_COUNT;

        control_plane->stats_sequence[index] += 1u;
        __dmb();
        uart_control_plane_service_pending(control_plane, (uart_port_id_t)index);
        __dmb();
        control_plane->stats_sequence[index] += 1u;
    }

    /* uart_driver_poll_io() advances poll_start_index after this sweep so the
     * next worker step starts control and I/O on the same port. */
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