/**
 * @file uart_driver.c
 * @brief Logical UART port table for the PicoUart firmware.
 */

#include "uart/uart_driver.h"

#include "board/uart_board.h"
#include "uart/backend.h"
#include "uart/backend_policy.h"
#include "uart/control_pending.h"
#include "uart/hw/hw_uart_driver.h"
#include "uart/line_coding.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "uart/pio/internal.h"
#include "uart/ring_buffer/ring_buffer.h"
#include "uart/worker_health.h"

#include <string.h>

/** @brief Maximum time the worker may defer applying a line-coding change. */
#define UART_DRIVER_CONTROL_APPLY_TIMEOUT_MS 1000u
/** @brief Maximum attempts to acquire a coherent worker-owned telemetry snapshot. */
#define UART_DRIVER_PORT_STATS_SNAPSHOT_ATTEMPTS 3u
/** @brief Maximum RX snapshot copied before handing bytes to a USB writer. */
#define UART_DRIVER_RX_SNAPSHOT_SIZE 1024u

/**
 * @brief Single-slot mailbox shared between the USB core and the UART worker core.
 */
typedef struct {
    volatile uint32_t request_sequence; /**< Monotonic request sequence published by core 0. */
    volatile uint32_t response_sequence; /**< Latest completed request sequence published by core 1. */
    volatile uint32_t port_id; /**< Port argument used by parameterized commands. */
    volatile uint32_t control_generation; /**< Host request generation associated with the command. */
    volatile uint32_t tx_boundary_sequence; /**< Last TX byte admitted before ingress paused. */
    volatile uart_driver_line_coding_t line_coding; /**< Pending line-coding payload. */
} uart_driver_mailbox_t;

/**
 * @brief Runtime storage for one logical UART port.
 */
typedef struct {
    uart_driver_port_info_t info; /**< Public metadata for the logical port. */
    const uart_backend_ops_t *ops; /**< Operations for the active backend storage. */
    union {
        hw_uart_driver_t hw; /**< Hardware UART backend state. */
        pio_uart_driver_t pio; /**< PIO UART backend state. */
    } backend; /**< Backend storage owned by the logical port. */
} uart_driver_port_t;

/**
 * @brief Deferred worker-owned control state for one logical UART port.
 */
typedef struct {
    bool pending; /**< True while a line-coding change is waiting to be applied. */
    uint32_t control_generation; /**< Host request generation that owns @ref pending. */
    uint32_t tx_boundary_sequence; /**< Last TX byte admitted under the old line format. */
    absolute_time_t deadline; /**< Absolute time when a deferred apply must succeed or fail. */
    uart_driver_line_coding_t line_coding; /**< Latest requested line-coding payload. */
} uart_driver_pending_control_t;

/** @brief Runtime state for the 6-port bridge, initialized from the board map. */
static uart_driver_port_t uart_ports[UART_PORT_COUNT];

/** @brief Core-to-core mailbox used for UART control operations. */
static uart_driver_mailbox_t uart_driver_mailbox;
/** @brief True after the dedicated UART worker core has been launched. */
static bool uart_driver_worker_started;
/** @brief Worker-loop counter published for core-0 watchdog gating. */
static volatile uint32_t uart_driver_worker_heartbeat;
/** @brief Per-port status flags for monitoring and HID reporting. */
static volatile uint8_t uart_driver_port_status_flags[UART_PORT_COUNT];
/** @brief Cross-core lock protecting @ref uart_driver_port_status_flags. */
static spin_lock_t *uart_driver_status_lock;
/** @brief Deferred line-coding requests owned by the worker core. */
static uart_driver_pending_control_t uart_driver_pending_controls[UART_PORT_COUNT];
/** @brief CDC requests waiting on core 0 for the worker mailbox. */
static bool uart_driver_soft_pending_controls[UART_PORT_COUNT];
/** @brief Latest host control-request generation for each port. */
static uint32_t uart_driver_control_generations[UART_PORT_COUNT];
/** @brief Per-port sequence counters for coherent core-0 telemetry snapshots. */
static volatile uint32_t uart_driver_port_stats_sequence[UART_PORT_COUNT];
/** @brief Worker-loop port index that starts the next backend poll sweep. */
static size_t uart_driver_poll_start_index;
static bool uart_driver_init_backends(void);
static void uart_driver_rollback_initialized_backends(void);
static void uart_driver_load_board_config(void);
static void uart_driver_poll_backends(void);
static void uart_driver_poll_io(void);
static bool uart_driver_line_coding_matches_current(const uart_driver_port_t *port,
                                                    const uart_driver_line_coding_t *line_coding);
static void uart_driver_set_line_coding_local(
    uart_port_id_t port_id,
    const uart_driver_line_coding_t *line_coding,
    uint32_t control_generation,
    uint32_t tx_boundary_sequence);
static void uart_driver_service_pending_control(uart_port_id_t port_id, uart_driver_port_t *port);
static void uart_driver_set_worker_control_pending(uart_port_id_t port_id, uint32_t control_generation);
static void uart_driver_finish_worker_control(uart_port_id_t port_id,
                                              uint32_t control_generation,
                                              bool success);
static void uart_driver_finish_mailbox_control(uart_port_id_t port_id,
                                               uint32_t control_generation,
                                               bool success);
static bool uart_driver_mailbox_has_pending_port(uart_port_id_t port_id);
static bool uart_driver_tx_boundary_drained(uart_driver_port_t *port,
                                            uint32_t boundary_sequence);
static bool uart_driver_rx_snapshot_is_current(const uart_driver_port_t *port,
                                               uint32_t consumer_sequence);

static void uart_driver_begin_port_stats_update(uart_port_id_t port_id)
{
    uart_driver_port_stats_sequence[port_id] += 1u;
    __dmb();
}

static void uart_driver_end_port_stats_update(uart_port_id_t port_id)
{
    __dmb();
    uart_driver_port_stats_sequence[port_id] += 1u;
}

static void uart_driver_set_port_status_flag(uart_port_id_t port_id, uint8_t flag)
{
    uint32_t save;

    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    save = spin_lock_blocking(uart_driver_status_lock);
    uart_driver_port_status_flags[port_id] |= flag;
    spin_unlock(uart_driver_status_lock, save);
}

static void uart_driver_clear_port_status_flag(uart_port_id_t port_id, uint8_t flag)
{
    uint32_t save;

    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    save = spin_lock_blocking(uart_driver_status_lock);
    uart_driver_port_status_flags[port_id] &= (uint8_t)~flag;
    spin_unlock(uart_driver_status_lock, save);
}

static void uart_driver_set_worker_control_pending(uart_port_id_t port_id,
                                                   uint32_t control_generation)
{
    uint32_t save = spin_lock_blocking(uart_driver_status_lock);

    uart_driver_pending_controls[port_id].pending = true;
    uart_driver_pending_controls[port_id].control_generation = control_generation;
    uart_driver_port_status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    if (uart_control_completion_is_current(control_generation,
                                           uart_driver_control_generations[port_id])) {
        uart_driver_port_status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    }
    spin_unlock(uart_driver_status_lock, save);
}

static void uart_driver_finish_worker_control(uart_port_id_t port_id,
                                              uint32_t control_generation,
                                              bool success)
{
    uint32_t save = spin_lock_blocking(uart_driver_status_lock);

    uart_driver_pending_controls[port_id].pending = false;
    if (uart_control_pending_should_clear(uart_driver_soft_pending_controls[port_id],
                                          uart_driver_mailbox_has_pending_port(port_id))) {
        uart_driver_port_status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    }
    uart_control_apply_completion_error(&uart_driver_port_status_flags[port_id],
                                        UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                                        control_generation,
                                        uart_driver_control_generations[port_id],
                                        success);
    spin_unlock(uart_driver_status_lock, save);
}

static void uart_driver_finish_mailbox_control(uart_port_id_t port_id,
                                               uint32_t control_generation,
                                               bool success)
{
    uint32_t save = spin_lock_blocking(uart_driver_status_lock);

    if (!uart_driver_soft_pending_controls[port_id]) {
        uart_driver_port_status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    }
    uart_control_apply_completion_error(&uart_driver_port_status_flags[port_id],
                                        UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                                        control_generation,
                                        uart_driver_control_generations[port_id],
                                        success);
    spin_unlock(uart_driver_status_lock, save);
}

static bool uart_driver_mailbox_has_pending_port(uart_port_id_t port_id)
{
    uint32_t request_sequence = uart_driver_mailbox.request_sequence;

    if (request_sequence == uart_driver_mailbox.response_sequence) {
        return false;
    }

    __dmb();
    return uart_driver_mailbox.port_id == (uint32_t)port_id;
}

static void uart_driver_worker_core_main(void)
{
    uart_backend_enable_rx_dma_irq();

    while (true) {
        uint32_t request_sequence = uart_driver_mailbox.request_sequence;

        if (request_sequence != uart_driver_mailbox.response_sequence) {
            uint32_t control_generation;
            uint32_t tx_boundary_sequence;
            uart_driver_line_coding_t line_coding;

            __dmb();
            control_generation = uart_driver_mailbox.control_generation;
            tx_boundary_sequence = uart_driver_mailbox.tx_boundary_sequence;
            line_coding = uart_driver_mailbox.line_coding;
            uart_driver_set_line_coding_local((uart_port_id_t)uart_driver_mailbox.port_id,
                                              &line_coding,
                                              control_generation,
                                              tx_boundary_sequence);

            __dmb();
            uart_driver_mailbox.response_sequence = request_sequence;
        }

        uart_driver_poll_backends();
        uart_driver_poll_io();
        uart_driver_worker_heartbeat += 1u;
        __dmb();
        tight_loop_contents();
    }
}

static bool uart_driver_line_coding_matches_current(const uart_driver_port_t *port,
                                                    const uart_driver_line_coding_t *line_coding)
{
    if ((port == NULL) || (port->ops == NULL) || (line_coding == NULL)) {
        return false;
    }

    return port->ops->line_coding_matches(&port->backend, line_coding);
}

static uart_driver_port_t *uart_driver_port_mutable(uart_port_id_t port_id)
{
    if (port_id >= UART_PORT_COUNT) {
        return NULL;
    }

    return &uart_ports[port_id];
}

static void uart_driver_load_board_config(void)
{
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        const uart_board_port_config_t *board_port = &uart_board_ports[index];
        uart_driver_port_t *port = &uart_ports[index];

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

static ring_buffer_t *uart_driver_rx_ring_mutable(uart_driver_port_t *port)
{
    if ((port == NULL) || (port->ops == NULL)) {
        return NULL;
    }

    return port->ops->rx_ring(&port->backend);
}

static ring_buffer_t *uart_driver_tx_ring_mutable(uart_driver_port_t *port)
{
    if ((port == NULL) || (port->ops == NULL)) {
        return NULL;
    }

    return port->ops->tx_ring(&port->backend);
}

static bool uart_driver_tx_boundary_drained(uart_driver_port_t *port,
                                            uint32_t boundary_sequence)
{
    ring_buffer_t *tx_ring = uart_driver_tx_ring_mutable(port);

    return (tx_ring != NULL) &&
           uart_control_tx_boundary_drained(tx_ring->consumer, boundary_sequence);
}

static bool uart_driver_rx_snapshot_is_current(const uart_driver_port_t *port,
                                               uint32_t consumer_sequence)
{
    if ((port == NULL) || (port->ops == NULL)) {
        return false;
    }

    return port->ops->rx_snapshot_is_current(&port->backend, consumer_sequence);
}

size_t uart_driver_port_count(void)
{
    return UART_PORT_COUNT;
}

static bool uart_driver_init_backends(void)
{
    bool init_ok = true;

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_driver_port_t *port = &uart_ports[index];
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
        uart_driver_port_t *port = &uart_ports[index];

        if ((port->ops != NULL) && port->ops->is_initialized(&port->backend)) {
            port->ops->deinit(&port->backend);
        }

        uart_driver_port_status_flags[index] = 0u;
    }
}


static void uart_driver_poll_backends(void)
{
    for (size_t offset = 0u; offset < UART_PORT_COUNT; ++offset) {
        size_t index = (uart_driver_poll_start_index + offset) % UART_PORT_COUNT;
        uart_driver_port_t *port = &uart_ports[index];

        uart_driver_begin_port_stats_update((uart_port_id_t)index);
        uart_driver_service_pending_control((uart_port_id_t)index, port);
        uart_driver_end_port_stats_update((uart_port_id_t)index);
    }

    uart_driver_poll_start_index = (uart_driver_poll_start_index + 1u) % UART_PORT_COUNT;
}

static void uart_driver_poll_io(void)
{
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_driver_port_t *port = &uart_ports[index];

        if ((port->ops != NULL) && port->ops->is_initialized(&port->backend)) {
            bool tx_launch_allowed = !uart_driver_pending_controls[index].pending ||
                                     !uart_driver_tx_boundary_drained(
                                         port,
                                         uart_driver_pending_controls[index].tx_boundary_sequence);
            uart_driver_begin_port_stats_update((uart_port_id_t)index);
            port->ops->poll(&port->backend, tx_launch_allowed);
            uart_driver_end_port_stats_update((uart_port_id_t)index);
        }
    }
}

static void uart_driver_service_pending_control(uart_port_id_t port_id, uart_driver_port_t *port)
{
    uart_driver_pending_control_t *pending_control;
    bool applied = false;

    if ((port == NULL) || (port_id >= UART_PORT_COUNT)) {
        return;
    }

    pending_control = &uart_driver_pending_controls[port_id];
    if (!pending_control->pending) {
        return;
    }

    if (!uart_driver_tx_boundary_drained(port, pending_control->tx_boundary_sequence)) {
        if (time_reached(pending_control->deadline)) {
            uart_driver_finish_worker_control(port_id, pending_control->control_generation, false);
        }
        return;
    }

    if (port->ops == NULL) {
        uart_driver_finish_worker_control(port_id, pending_control->control_generation, false);
        return;
    }
    applied = port->ops->set_line_coding(&port->backend, &pending_control->line_coding);

    if (!applied) {
        if (!time_reached(pending_control->deadline)) {
            return;
        }

        /* Continuous RX/TX activity can defer a PIO/HW reconfigure forever;
         * fail the request so USB ingress is not paused indefinitely. */
        uart_driver_finish_worker_control(port_id, pending_control->control_generation, false);
        return;
    }

    {
        uint32_t save = spin_lock_blocking(uart_driver_status_lock);
        port->info.baud_rate = port->ops->baud_rate(&port->backend);
        spin_unlock(uart_driver_status_lock, save);
    }
    uart_driver_finish_worker_control(port_id, pending_control->control_generation, true);
}

bool uart_driver_init(void)
{
    if (!uart_driver_worker_started) {
        if (uart_driver_status_lock == NULL) {
            uart_driver_status_lock = spin_lock_instance(spin_lock_claim_unused(true));
        }

        uart_driver_load_board_config();

        for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
            uart_driver_port_status_flags[index] = 0u;
            uart_driver_pending_controls[index].pending = false;
            uart_driver_soft_pending_controls[index] = false;
            uart_driver_control_generations[index] = 0u;
            uart_driver_port_stats_sequence[index] = 0u;
            uart_driver_pending_controls[index].deadline = nil_time;
            uart_driver_pending_controls[index].control_generation = 0u;
            uart_driver_pending_controls[index].tx_boundary_sequence = 0u;
            uart_driver_pending_controls[index].line_coding.baud_rate =
                PICO_UART_BOARD_DEFAULT_BAUD_RATE;
            uart_driver_pending_controls[index].line_coding.data_bits = 8u;
            uart_driver_pending_controls[index].line_coding.stop_bits = 1u;
            uart_driver_pending_controls[index].line_coding.parity = UART_DRIVER_PARITY_NONE;
        }

        uart_driver_mailbox.request_sequence = 0u;
        uart_driver_mailbox.response_sequence = 0u;
        uart_driver_mailbox.port_id = 0u;
        uart_driver_mailbox.control_generation = 0u;
        uart_driver_mailbox.tx_boundary_sequence = 0u;
        uart_driver_mailbox.line_coding.baud_rate = 0u;
        uart_driver_mailbox.line_coding.data_bits = 8u;
        uart_driver_mailbox.line_coding.stop_bits = 1u;
        uart_driver_mailbox.line_coding.parity = UART_DRIVER_PARITY_NONE;
        uart_driver_poll_start_index = 0u;
        uart_driver_worker_heartbeat = 0u;

        if (!uart_driver_init_backends()) {
            uart_driver_rollback_initialized_backends();
            return false;
        }

        for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
            uart_driver_port_t *port = &uart_ports[index];

            if (port->ops != NULL) {
                port->ops->clear_rx_error_baseline(&port->backend);
            }
        }

        multicore_launch_core1(uart_driver_worker_core_main);
        uart_driver_worker_started = true;
    }

    return true;
}

bool uart_driver_port_is_ready(uart_port_id_t port_id)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);

    if (port == NULL) {
        return false;
    }

    return (port->ops != NULL) && port->ops->is_initialized(&port->backend);
}

size_t uart_driver_drain_rx(uart_port_id_t port_id,
                            size_t capacity,
                            uint32_t (*writer)(void *context, const uint8_t *data, uint32_t length),
                            void *context)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    ring_buffer_t *rx_ring;
    size_t total_written = 0u;
    static uint8_t snapshot[UART_DRIVER_RX_SNAPSHOT_SIZE];

    if ((port == NULL) || !uart_driver_port_is_ready(port_id) || (writer == NULL)) {
        return 0u;
    }

    rx_ring = uart_driver_rx_ring_mutable(port);
    if (rx_ring == NULL) {
        return 0u;
    }

    (void)ring_buffer_recover_overflow(rx_ring);
    while ((capacity == 0u) || (total_written < capacity)) {
        ring_buffer_span_t span = ring_buffer_read_span(rx_ring);
        uint32_t offered;
        uint32_t written;

        if (span.length == 0u) {
            break;
        }

        offered = (uint32_t)span.length;
        if ((capacity != 0u) && (offered > (capacity - total_written))) {
            offered = (uint32_t)(capacity - total_written);
        }

        if (offered > sizeof(snapshot)) {
            offered = sizeof(snapshot);
        }

        uint32_t consumer_sequence = rx_ring->consumer_reserved_sequence;
        uint32_t stats_sequence = uart_driver_port_stats_sequence[port_id];

        if ((stats_sequence & 1u) != 0u) {
            return total_written;
        }

        memcpy(snapshot, span.data, offered);
        __dmb();
        if (!ring_buffer_read_span_is_current(rx_ring) ||
            !uart_driver_rx_snapshot_is_current(port, consumer_sequence) ||
            (uart_driver_port_stats_sequence[port_id] != stats_sequence)) {
            (void)ring_buffer_recover_overflow(rx_ring);
            return total_written;
        }

        written = writer(context, snapshot, offered);
        if (written == 0u) {
            break;
        }

        if ((written > offered) || !ring_buffer_commit_snapshot_consumed(rx_ring, written)) {
            return total_written;
        }

        total_written += written;
        if (written < offered) {
            break;
        }
    }

    return total_written;
}

size_t uart_driver_recover_rx(uart_port_id_t port_id)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    ring_buffer_t *rx_ring;

    if ((port == NULL) || !uart_driver_port_is_ready(port_id)) {
        return 0u;
    }

    rx_ring = uart_driver_rx_ring_mutable(port);
    if (rx_ring == NULL) {
        return 0u;
    }

    return ring_buffer_recover_overflow(rx_ring);
}

size_t uart_driver_fill_tx(uart_port_id_t port_id,
                           size_t capacity,
                           uint32_t (*reader)(void *context, uint8_t *data, uint32_t length),
                           void *context)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    ring_buffer_t *tx_ring;
    size_t total_read = 0u;

    if ((port == NULL) || !uart_driver_port_is_ready(port_id) || (reader == NULL) ||
        uart_driver_port_tx_is_blocked(port_id)) {
        return 0u;
    }

    tx_ring = uart_driver_tx_ring_mutable(port);
    if (tx_ring == NULL) {
        return 0u;
    }

    while ((capacity == 0u) || (total_read < capacity)) {
        ring_buffer_span_t span = ring_buffer_write_span(tx_ring);
        uint32_t offered;
        uint32_t read;

        if (span.length == 0u) {
            break;
        }

        offered = (uint32_t)span.length;
        if ((capacity != 0u) && (offered > (capacity - total_read))) {
            offered = (uint32_t)(capacity - total_read);
        }

        read = reader(context, span.data, offered);
        if (read == 0u) {
            break;
        }

        if ((read > offered) || !ring_buffer_commit_produced(tx_ring, read)) {
            return total_read;
        }

        total_read += read;
        if (read < offered) {
            break;
        }
    }

    return total_read;
}

static void uart_driver_set_line_coding_local(
    uart_port_id_t port_id,
    const uart_driver_line_coding_t *line_coding,
    uint32_t control_generation,
    uint32_t tx_boundary_sequence)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    /* Preserve unread RX bytes across a line-format change by restarting DMA at
     * the live producer index instead of resetting the shared RX ring.
     */
    uart_driver_pending_control_t *pending_control;
    bool was_pending;
    bool same_request;

    if (port == NULL) {
        return;
    }

    pending_control = &uart_driver_pending_controls[port_id];

    if (!uart_line_coding_is_valid(line_coding)) {
        uart_driver_finish_mailbox_control(port_id, control_generation, false);
        return;
    }

    was_pending = pending_control->pending;
    same_request = was_pending &&
                   (pending_control->line_coding.baud_rate == line_coding->baud_rate) &&
                   (pending_control->line_coding.data_bits == line_coding->data_bits) &&
                   (pending_control->line_coding.stop_bits == line_coding->stop_bits) &&
                   (pending_control->line_coding.parity == line_coding->parity);

    if (uart_driver_line_coding_matches_current(port, line_coding)) {
        if (pending_control->pending) {
            /* The newest request keeps the current format, so cancel the older deferred change. */
            uart_driver_finish_worker_control(port_id, pending_control->control_generation, true);
            uart_driver_finish_mailbox_control(port_id, control_generation, true);
        } else {
            uart_driver_finish_mailbox_control(port_id, control_generation, true);
        }
        return;
    }

    if ((port->ops == NULL) || !port->ops->line_coding_acceptable(line_coding)) {
        uart_driver_finish_mailbox_control(port_id, control_generation, false);
        return;
    }
    pending_control->line_coding = *line_coding;

    if (uart_control_worker_should_set_deadline(was_pending, same_request)) {
        pending_control->deadline = make_timeout_time_ms(UART_DRIVER_CONTROL_APPLY_TIMEOUT_MS);
    }
    pending_control->tx_boundary_sequence = tx_boundary_sequence;
    uart_driver_set_worker_control_pending(port_id, control_generation);
}

bool uart_driver_line_coding_acceptable(uart_port_id_t port_id,
                                        const uart_driver_line_coding_t *line_coding)
{
    uart_driver_port_info_t port_info;

    if (!uart_driver_port_info(port_id, &port_info) || !uart_line_coding_is_valid(line_coding)) {
        return false;
    }

    return uart_ports[port_id].ops != NULL &&
           uart_ports[port_id].ops->line_coding_acceptable(line_coding);
}

bool uart_driver_queue_line_coding(uart_port_id_t port_id,
                                   const uart_driver_line_coding_t *line_coding,
                                   uint32_t control_generation)
{
    uint32_t request_sequence;
    uint32_t save;
    uart_driver_port_t *port;
    ring_buffer_t *tx_ring;

    if (!uart_driver_worker_started) {
        return false;
    }

    if (!uart_driver_line_coding_acceptable(port_id, line_coding)) {
        uart_driver_report_control_error(port_id);
        return false;
    }

    port = uart_driver_port_mutable(port_id);
    tx_ring = uart_driver_tx_ring_mutable(port);
    if (tx_ring == NULL) {
        return false;
    }

    save = spin_lock_blocking(uart_driver_status_lock);
    if (!uart_control_mailbox_is_empty(uart_driver_mailbox.request_sequence,
                                       uart_driver_mailbox.response_sequence)) {
        spin_unlock(uart_driver_status_lock, save);
        return false;
    }

    request_sequence = uart_control_mailbox_next_sequence(uart_driver_mailbox.request_sequence);

    /* A later reject bumps the latest generation so this completion cannot
     * clear CONTROL_ERROR, but a prior valid request must still be applied. */
    uart_driver_soft_pending_controls[port_id] = false;
    uart_driver_port_status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    uart_driver_mailbox.port_id = (uint32_t)port_id;
    uart_driver_mailbox.control_generation = control_generation;
    uart_driver_mailbox.tx_boundary_sequence = tx_ring->producer;
    uart_driver_mailbox.line_coding = *line_coding;
    __dmb();
    uart_driver_mailbox.request_sequence = request_sequence;
    spin_unlock(uart_driver_status_lock, save);
    return true;
}

bool uart_driver_port_tx_is_blocked(uart_port_id_t port_id)
{
    bool blocked;
    uint32_t save;

    if (port_id >= UART_PORT_COUNT) {
        return true;
    }

    save = spin_lock_blocking(uart_driver_status_lock);
    blocked = uart_control_tx_should_block(uart_driver_soft_pending_controls[port_id],
                                           uart_driver_pending_controls[port_id].pending,
                                           uart_driver_mailbox_has_pending_port(port_id));
    spin_unlock(uart_driver_status_lock, save);
    return blocked;
}

void uart_driver_report_control_error(uart_port_id_t port_id)
{
    uint32_t save;

    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    save = spin_lock_blocking(uart_driver_status_lock);
    uart_control_apply_reject_error(&uart_driver_control_generations[port_id],
                                    &uart_driver_port_status_flags[port_id],
                                    UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
    spin_unlock(uart_driver_status_lock, save);
}

void uart_driver_report_soft_pending_error(uart_port_id_t port_id,
                                           uint32_t control_generation)
{
    uint32_t save;

    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    save = spin_lock_blocking(uart_driver_status_lock);
    uart_driver_soft_pending_controls[port_id] = false;
    if (uart_control_completion_is_current(control_generation,
                                           uart_driver_control_generations[port_id])) {
        uart_driver_port_status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    }
    if (uart_control_pending_should_clear(uart_driver_pending_controls[port_id].pending,
                                          uart_driver_mailbox_has_pending_port(port_id))) {
        uart_driver_port_status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    }
    spin_unlock(uart_driver_status_lock, save);
}

uint32_t uart_driver_mark_control_pending(uart_port_id_t port_id)
{
    uint32_t save;
    uint32_t control_generation;

    if (port_id >= UART_PORT_COUNT) {
        return 0u;
    }

    save = spin_lock_blocking(uart_driver_status_lock);
    uart_driver_control_generations[port_id] += 1u;
    control_generation = uart_driver_control_generations[port_id];
    uart_driver_soft_pending_controls[port_id] = true;
    uart_driver_port_status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    uart_driver_port_status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    spin_unlock(uart_driver_status_lock, save);
    return control_generation;
}

uint8_t uart_driver_port_status(uart_port_id_t port_id)
{
    uint8_t status;
    uint32_t save;

    if (port_id >= UART_PORT_COUNT) {
        return 0u;
    }

    save = spin_lock_blocking(uart_driver_status_lock);
    status = uart_driver_port_status_flags[port_id];
    spin_unlock(uart_driver_status_lock, save);
    return status;
}

bool uart_driver_worker_is_running(void)
{
    return uart_driver_worker_started;
}

bool uart_driver_worker_heartbeat_is_fresh(void)
{
    static uint32_t last_heartbeat;
    static uint32_t last_change_ms;
    uint32_t heartbeat;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    __dmb();
    heartbeat = uart_driver_worker_heartbeat;
    return uart_worker_heartbeat_is_fresh(heartbeat,
                                          &last_heartbeat,
                                          now_ms,
                                          &last_change_ms,
                                          UART_WORKER_HEARTBEAT_STALE_MS);
}

bool uart_driver_port_info(uart_port_id_t port_id, uart_driver_port_info_t *info)
{
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (info == NULL) || (uart_driver_status_lock == NULL)) {
        return false;
    }

    save = spin_lock_blocking(uart_driver_status_lock);
    *info = uart_ports[port_id].info;
    spin_unlock(uart_driver_status_lock, save);
    return true;
}

bool uart_driver_port_stats(uart_port_id_t port_id, uart_driver_port_stats_t *stats)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    uart_backend_stats_t backend_stats;
    ring_buffer_t *rx_ring;
    ring_buffer_t *tx_ring;
    uint32_t first_sequence;
    uint32_t second_sequence;

    if ((port == NULL) || (stats == NULL)) {
        return false;
    }

    rx_ring = uart_driver_rx_ring_mutable(port);
    tx_ring = uart_driver_tx_ring_mutable(port);
    if ((rx_ring == NULL) || (tx_ring == NULL)) {
        return false;
    }

    for (size_t attempt = 0u; attempt < UART_DRIVER_PORT_STATS_SNAPSHOT_ATTEMPTS; ++attempt) {
        first_sequence = uart_driver_port_stats_sequence[port_id];
        __dmb();
        if ((first_sequence & 1u) != 0u) {
            continue;
        }

        stats->controller_tx_bytes = 0u;
        stats->controller_rx_bytes = 0u;
        stats->tx_ring_high_watermark = (uint16_t)ring_buffer_high_watermark(tx_ring);
        stats->rx_ring_high_watermark = (uint16_t)ring_buffer_high_watermark(rx_ring);
        stats->tx_ring_overflow_count = (uint32_t)ring_buffer_overflow_count(tx_ring);
        stats->rx_ring_overflow_count = (uint32_t)ring_buffer_overflow_count(rx_ring);
        stats->rx_ring_pending_overflow_count = (uint32_t)ring_buffer_pending_overflow(rx_ring);
        stats->rx_error_count = 0u;

        if (port->ops == NULL) {
            return false;
        }
        backend_stats = port->ops->stats(&port->backend);
        stats->controller_tx_bytes = backend_stats.controller_tx_bytes;
        stats->controller_rx_bytes = backend_stats.controller_rx_bytes;
        stats->rx_error_count = backend_stats.rx_error_count;

        __dmb();
        second_sequence = uart_driver_port_stats_sequence[port_id];
        if ((first_sequence == second_sequence) && ((second_sequence & 1u) == 0u)) {
            return true;
        }
    }

    return false;
}

void uart_driver_reset_soft_pending(uart_port_id_t port_id)
{
    uint32_t save;

    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    save = spin_lock_blocking(uart_driver_status_lock);
    uart_driver_soft_pending_controls[port_id] = false;
    if (uart_control_pending_should_clear(uart_driver_pending_controls[port_id].pending,
                                          uart_driver_mailbox_has_pending_port(port_id))) {
        uart_driver_port_status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    }
    spin_unlock(uart_driver_status_lock, save);
}
