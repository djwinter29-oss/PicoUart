/**
 * @file uart_driver.c
 * @brief Logical UART port table for the PicoUart firmware.
 */

#include "uart/uart_driver.h"

#include "board/uart_board.h"
#include "uart/backend/adapter.h"
#include "uart/control/mailbox.h"
#include "uart/control/plane.h"
#include "uart/control/ownership.h"
#include "uart/driver_state.h"
#include "uart/hw/hw_uart_driver.h"
#include "uart/line_coding.h"
#include "uart/port_api.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "uart/pio/pio_uart_driver_internal.h"
#include "uart/runtime.h"
#include "uart/worker.h"
#include "uart/worker_health.h"

/** @brief Single owner for top-level UART facade state. */
static uart_driver_state_t uart_driver_state;
static bool uart_driver_init_backends(void);
static void uart_driver_rollback_initialized_backends(void);
static void uart_driver_load_board_config(void);
static void uart_driver_poll_io(void);
static void uart_driver_worker_initialize(void);
static void uart_driver_worker_service_control(void);
static void uart_driver_worker_core_main(void);
static bool uart_driver_mailbox_has_pending_port(uart_port_id_t port_id);
static void uart_driver_bind_state_views(void);

static void uart_driver_begin_port_stats_update(uart_port_id_t port_id)
{
    uart_driver_state.stats_sequence[port_id] += 1u;
    __dmb();
}

static void uart_driver_end_port_stats_update(uart_port_id_t port_id)
{
    __dmb();
    uart_driver_state.stats_sequence[port_id] += 1u;
}

static void uart_driver_set_port_status_flag(uart_port_id_t port_id, uint8_t flag)
{
    uint32_t save;

    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_driver_state.status_flags[port_id] |= flag;
    spin_unlock(uart_driver_state.status_lock, save);
}

static void uart_driver_clear_port_status_flag(uart_port_id_t port_id, uint8_t flag)
{
    uint32_t save;

    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_driver_state.status_flags[port_id] &= (uint8_t)~flag;
    spin_unlock(uart_driver_state.status_lock, save);
}

static bool uart_driver_mailbox_has_pending_port(uart_port_id_t port_id)
{
    return (port_id < UART_PORT_COUNT) &&
           uart_control_mailbox_has_pending_port(&uart_driver_state.mailboxes[port_id],
                                                 (uint32_t)port_id);
}

static const uart_worker_hooks_t uart_driver_worker_hooks = {
    .initialize = uart_driver_worker_initialize,
    .service_control = uart_driver_worker_service_control,
    .poll_io = uart_driver_poll_io,
    .heartbeat = &uart_driver_state.worker_heartbeat,
};

static void uart_driver_worker_initialize(void)
{
    uart_backend_enable_rx_dma_irq();
}

static void uart_driver_worker_service_control(void)
{
    uart_control_plane_service(&uart_driver_state.control_plane);
}

static void uart_driver_bind_state_views(void)
{
    uart_driver_state.control_plane.ports = uart_driver_state.ports;
    uart_driver_state.control_plane.mailboxes = uart_driver_state.mailboxes;
    uart_driver_state.control_plane.pending_controls = uart_driver_state.pending_controls;
    uart_driver_state.control_plane.soft_pending_controls =
        uart_driver_state.soft_pending_controls;
    uart_driver_state.control_plane.control_generations = uart_driver_state.control_generations;
    uart_driver_state.control_plane.status_flags = uart_driver_state.status_flags;
    uart_driver_state.control_plane.status_lock = uart_driver_state.status_lock;
    uart_driver_state.control_plane.stats_sequence = uart_driver_state.stats_sequence;
    uart_driver_state.control_plane.poll_start_index = &uart_driver_state.poll_start_index;

    uart_driver_state.port_api.ports = uart_driver_state.ports;
    uart_driver_state.port_api.stats_sequence = uart_driver_state.stats_sequence;
    uart_driver_state.port_api.status_flags = uart_driver_state.status_flags;
    uart_driver_state.port_api.status_lock = uart_driver_state.status_lock;
}

static void uart_driver_worker_core_main(void)
{
    uart_worker_run(&uart_driver_worker_hooks);
}

static uart_runtime_port_t *uart_driver_port_mutable(uart_port_id_t port_id)
{
    if (port_id >= UART_PORT_COUNT) {
        return NULL;
    }

    return &uart_driver_state.ports[port_id];
}

static void uart_driver_load_board_config(void)
{
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        const uart_board_port_config_t *board_port = &uart_board_ports[index];
        uart_runtime_port_t *port = &uart_driver_state.ports[index];

        port->info = board_port->info;
        port->ops = uart_backend_ops_for_type(board_port->info.backend);
        if (board_port->info.backend == UART_DRIVER_BACKEND_HW) {
            port->backend.hw.config = board_port->backend.hw;
            port->backend.hw.initialized = false;
        } else {
            port->backend.pio.config = board_port->backend.pio;
            port->backend.pio.initialized = false;
        }
    }
}

static bool uart_driver_init_backends(void)
{
    bool init_ok = true;

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_runtime_port_t *port = &uart_driver_state.ports[index];
        bool port_ok = false;

        port_ok = (port->ops != NULL) &&
                  (port->ops->is_initialized(&port->backend) || port->ops->init(&port->backend));

        if (!port_ok) {
            init_ok = false;
            uart_driver_clear_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_READY);
            uart_driver_set_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_INIT_FAILED);
            continue;
        }

        uart_driver_clear_port_status_flag((uart_port_id_t)index,
                                           UART_DRIVER_PORT_STATUS_INIT_FAILED |
                                               UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
        port->info.baud_rate = port->ops->baud_rate(&port->backend);
        uart_driver_set_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_READY);
    }

    return init_ok;
}

static void uart_driver_rollback_initialized_backends(void)
{
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_runtime_port_t *port = &uart_driver_state.ports[index];

        if ((port->ops != NULL) && port->ops->is_initialized(&port->backend)) {
            port->ops->deinit(&port->backend);
        }

        uart_driver_state.status_flags[index] = 0u;
    }
}

static void uart_driver_poll_io(void)
{
    for (size_t offset = 0u; offset < UART_PORT_COUNT; ++offset) {
        size_t index = (uart_driver_state.poll_start_index + offset) % UART_PORT_COUNT;
        uart_runtime_port_t *port = &uart_driver_state.ports[index];

        if ((port->ops != NULL) && port->ops->is_initialized(&port->backend)) {
            bool tx_launch_allowed = uart_control_plane_tx_launch_allowed(
                &uart_driver_state.control_plane, (uart_port_id_t)index);
            uart_driver_begin_port_stats_update((uart_port_id_t)index);
            port->ops->poll(&port->backend, tx_launch_allowed);
            uart_driver_end_port_stats_update((uart_port_id_t)index);
        }
    }
}

bool uart_driver_init(void)
{
    if (!uart_driver_state.worker_started) {
        if (uart_driver_state.status_lock == NULL) {
            uart_driver_state.status_lock = spin_lock_instance(spin_lock_claim_unused(true));
        }
        uart_driver_bind_state_views();

        uart_driver_load_board_config();

        for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
            uart_driver_state.status_flags[index] = 0u;
            uart_driver_state.pending_controls[index].pending = false;
            uart_driver_state.soft_pending_controls[index] = false;
            uart_driver_state.control_generations[index] = 0u;
            uart_driver_state.stats_sequence[index] = 0u;
            uart_driver_state.pending_controls[index].deadline = nil_time;
            uart_driver_state.pending_controls[index].control_generation = 0u;
            uart_driver_state.pending_controls[index].tx_boundary_sequence = 0u;
            uart_driver_state.pending_controls[index].line_coding.baud_rate =
                PICO_UART_BOARD_DEFAULT_BAUD_RATE;
            uart_driver_state.pending_controls[index].line_coding.data_bits = 8u;
            uart_driver_state.pending_controls[index].line_coding.stop_bits = 1u;
            uart_driver_state.pending_controls[index].line_coding.parity = UART_DRIVER_PARITY_NONE;
        }

        for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
            uart_control_mailbox_reset(&uart_driver_state.mailboxes[index]);
        }
        uart_driver_state.poll_start_index = 0u;
        uart_driver_state.worker_heartbeat = 0u;

        if (!uart_driver_init_backends()) {
            uart_driver_rollback_initialized_backends();
            return false;
        }

        for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
            uart_runtime_port_t *port = &uart_driver_state.ports[index];

            if (port->ops != NULL) {
                port->ops->clear_rx_error_baseline(&port->backend);
            }
        }

        multicore_launch_core1(uart_driver_worker_core_main);
        uart_driver_state.worker_started = true;
    }

    return true;
}

size_t uart_driver_port_count(void)
{
    return uart_port_api_count();
}

bool uart_driver_port_is_ready(uart_port_id_t port_id)
{
    return uart_port_api_is_ready(&uart_driver_state.port_api, port_id);
}

size_t uart_driver_drain_rx(uart_port_id_t port_id,
                            size_t capacity,
                            uint32_t (*writer)(void *context, const uint8_t *data, uint32_t length),
                            void *context)
{
    return uart_port_api_drain_rx(&uart_driver_state.port_api, port_id, capacity, writer, context);
}

size_t uart_driver_recover_rx(uart_port_id_t port_id)
{
    return uart_port_api_recover_rx(&uart_driver_state.port_api, port_id);
}

size_t uart_driver_fill_tx(uart_port_id_t port_id,
                           size_t capacity,
                           uint32_t (*reader)(void *context, uint8_t *data, uint32_t length),
                           void *context)
{
    return uart_port_api_fill_tx(&uart_driver_state.port_api, port_id, capacity, reader, context);
}

bool uart_driver_line_coding_acceptable(uart_port_id_t port_id,
                                        const uart_driver_line_coding_t *line_coding)
{
    uart_driver_port_info_t port_info;

    if (!uart_driver_port_info(port_id, &port_info) || !uart_line_coding_is_valid(line_coding)) {
        return false;
    }

    return uart_driver_state.ports[port_id].ops != NULL &&
           uart_driver_state.ports[port_id].ops->line_coding_acceptable(line_coding);
}

bool uart_driver_queue_line_coding(uart_port_id_t port_id,
                                   const uart_driver_line_coding_t *line_coding,
                                   uint32_t control_generation)
{
    uint32_t save;
    uart_runtime_port_t *port;
    ring_buffer_t *tx_ring;
    uart_control_mailbox_request_t request;

    if (!uart_driver_state.worker_started) {
        return false;
    }

    if (!uart_driver_line_coding_acceptable(port_id, line_coding)) {
        uart_driver_report_control_error(port_id);
        return false;
    }

    port = uart_driver_port_mutable(port_id);
    tx_ring = (port != NULL) && (port->ops != NULL) ? port->ops->tx_ring(&port->backend) : NULL;
    if (tx_ring == NULL) {
        return false;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    if (!uart_control_mailbox_can_publish(&uart_driver_state.mailboxes[port_id])) {
        spin_unlock(uart_driver_state.status_lock, save);
        return false;
    }

    /* A later reject bumps the latest generation so this completion cannot
     * clear CONTROL_ERROR, but a prior valid request must still be applied. */
    uart_driver_state.soft_pending_controls[port_id] = false;
    uart_driver_state.status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    request = (uart_control_mailbox_request_t){
        .port_id = (uint32_t)port_id,
        .control_generation = control_generation,
        .tx_boundary_sequence = tx_ring->producer,
        .line_coding = *line_coding,
    };
    (void)uart_control_mailbox_publish(&uart_driver_state.mailboxes[port_id], &request);
    spin_unlock(uart_driver_state.status_lock, save);
    return true;
}

bool uart_driver_port_tx_is_blocked(uart_port_id_t port_id)
{
    bool blocked;
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return true;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    blocked = uart_control_tx_should_block(uart_driver_state.status_flags[port_id],
                                           UART_DRIVER_PORT_STATUS_CONTROL_PENDING);
    spin_unlock(uart_driver_state.status_lock, save);
    return blocked;
}

void uart_driver_report_control_error(uart_port_id_t port_id)
{
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_control_apply_reject_error(&uart_driver_state.control_generations[port_id],
                                    &uart_driver_state.status_flags[port_id],
                                    UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
    spin_unlock(uart_driver_state.status_lock, save);
}

void uart_driver_report_soft_pending_error(uart_port_id_t port_id,
                                           uint32_t control_generation)
{
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_driver_state.soft_pending_controls[port_id] = false;
    if (uart_control_completion_is_current(control_generation,
                                           uart_driver_state.control_generations[port_id])) {
        uart_driver_state.status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    }
    if (uart_control_pending_should_clear(uart_driver_state.pending_controls[port_id].pending,
                                          uart_driver_mailbox_has_pending_port(port_id))) {
        uart_driver_state.status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    }
    spin_unlock(uart_driver_state.status_lock, save);
}

uint32_t uart_driver_mark_control_pending(uart_port_id_t port_id)
{
    uint32_t save;
    uint32_t control_generation;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return 0u;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_driver_state.control_generations[port_id] += 1u;
    control_generation = uart_driver_state.control_generations[port_id];
    uart_driver_state.soft_pending_controls[port_id] = true;
    uart_driver_state.status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    uart_driver_state.status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    spin_unlock(uart_driver_state.status_lock, save);
    return control_generation;
}

uint8_t uart_driver_port_status(uart_port_id_t port_id)
{
    uint8_t status;
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return 0u;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    status = uart_driver_state.status_flags[port_id];
    spin_unlock(uart_driver_state.status_lock, save);
    return status;
}

bool uart_driver_worker_is_running(void)
{
    return uart_driver_state.worker_started;
}

bool uart_driver_worker_heartbeat_is_fresh(void)
{
    static uint32_t last_heartbeat;
    static uint32_t last_change_ms;
    uint32_t heartbeat;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    __dmb();
    heartbeat = uart_driver_state.worker_heartbeat;
    return uart_worker_heartbeat_is_fresh(heartbeat,
                                          &last_heartbeat,
                                          now_ms,
                                          &last_change_ms,
                                          UART_WORKER_HEARTBEAT_STALE_MS);
}

bool uart_driver_port_info(uart_port_id_t port_id, uart_driver_port_info_t *info)
{
    return uart_port_api_info(&uart_driver_state.port_api, port_id, info);
}

bool uart_driver_port_stats(uart_port_id_t port_id, uart_driver_port_stats_t *stats)
{
    return uart_port_api_stats(&uart_driver_state.port_api, port_id, stats);
}

void uart_driver_reset_soft_pending(uart_port_id_t port_id)
{
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_driver_state.soft_pending_controls[port_id] = false;
    if (uart_control_pending_should_clear(uart_driver_state.pending_controls[port_id].pending,
                                          uart_driver_mailbox_has_pending_port(port_id))) {
        uart_driver_state.status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    }
    spin_unlock(uart_driver_state.status_lock, save);
}
