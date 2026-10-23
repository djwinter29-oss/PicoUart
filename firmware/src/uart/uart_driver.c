/**
 * @file uart_driver.c
 * @brief Logical UART port table for the PicoUart firmware.
 */

#include "uart/uart_driver.h"
#include "platform/service_load.h"

#include "uart/backend/backend.h"
#include "uart/bridge.h"
#include "uart/control/mailbox.h"
#include "uart/control/plane.h"
#include "uart/control/ownership.h"
#include "uart/hw/hw_uart_driver.h"
#include "uart/line_coding.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "uart/pio/pio_uart_driver_internal.h"
#include "uart/runtime.h"
#include "uart/worker.h"

/** @brief Maximum attempts to acquire a coherent worker-owned telemetry snapshot. */
#define UART_DRIVER_STATS_SNAPSHOT_ATTEMPTS 3u

/** @brief Fixed per-port RX storage; only the DMA arrays carry ring-size alignment. */
typedef union {
    uint8_t hw[PICO_UART_HW_UART_RX_BUFFER_SIZE]
        __attribute__((aligned(PICO_UART_HW_UART_RX_BUFFER_SIZE))); /**< Hardware UART DMA ring storage. */
    uint8_t pio[PICO_UART_PIO_UART_RX_BUFFER_SIZE]
        __attribute__((aligned(PICO_UART_PIO_UART_RX_BUFFER_SIZE))); /**< PIO UART DMA ring storage. */
} uart_driver_rx_storage_t;

/** @brief Lifetime RX storage, separate from unaligned runtime metadata and TX rings. */
static uart_driver_rx_storage_t uart_driver_rx_storage[UART_PORT_COUNT];

_Static_assert(_Alignof(uart_runtime_port_t) < _Alignof(uart_driver_rx_storage_t),
               "Logical-port metadata must not inherit DMA ring alignment");

/** @brief Per-port lifecycle requests protected by the shared status lock. */
typedef struct {
    bool stop_pending;  /**< Stop requested; takes precedence over a queued start. */
    bool usb_quiesced;  /**< Core 0 stopped bridging and cleared the old CDC FIFOs. */
    bool start_pending; /**< Start requested by core 0; consumed once by the worker. */
} uart_driver_lifecycle_t;

/** @brief Driver-owned backend storage, lifecycle requests, and shared control/telemetry state. */
typedef struct {
    uart_runtime_port_t ports[UART_PORT_COUNT];                /**< Backend storage and public metadata. */
    uart_control_mailbox_t mailboxes[UART_PORT_COUNT];         /**< One core-0 to core-1 control slot per port. */
    bool worker_started;                                      /**< True after core 1 has been launched. */
    volatile uint32_t worker_heartbeat;                        /**< Core-1 worker progress counter. */
    volatile uint8_t status_flags[UART_PORT_COUNT];            /**< Per-port health flags. */
    spin_lock_t *status_lock;                                 /**< Lock protecting shared status and generations. */
    uart_control_pending_t pending_controls[UART_PORT_COUNT];  /**< Worker-owned control state. */
    bool soft_pending_controls[UART_PORT_COUNT];               /**< Core-0 mailbox wait state. */
    uart_driver_lifecycle_t lifecycle[UART_PORT_COUNT];        /**< Independent per-port start/stop handshakes. */
    uint32_t control_generations[UART_PORT_COUNT];              /**< Latest host generation per port. */
    volatile uint32_t stats_sequence[UART_PORT_COUNT];         /**< Coherent stats snapshot sequence. */
    size_t poll_start_index;                                  /**< First port of the next worker step. */
    uart_control_plane_t control_plane;                       /**< Worker-side deferred control context. */
} uart_driver_state_t;

/** @brief Single owner for top-level UART facade state. */
static uart_driver_state_t uart_driver_state;
#if PICO_UART_PROFILE_TIMING
/** @brief Maximum observed interval between backend polls per port, in microseconds, for SWD profiling. */
volatile uint32_t uart_driver_max_service_gap_us[UART_PORT_COUNT];
static uint32_t uart_driver_previous_service_us[UART_PORT_COUNT];
#endif
static bool uart_driver_init_backends(void);
static void uart_driver_rollback_initialized_backends(void);
static void uart_driver_load_board_config(void);
static void uart_driver_poll_io(void);
static void uart_driver_worker_initialize(void);
static void uart_driver_worker_service_control(void);
static void uart_driver_worker_core_main(void);
static void uart_driver_service_channel_lifecycle(void);
static bool uart_driver_mailbox_has_pending_port(uart_port_id_t port_id);
static void uart_driver_bind_state_views(void);

static uart_runtime_port_t *uart_driver_port(uart_port_id_t port_id)
{
    return port_id < UART_PORT_COUNT ? &uart_driver_state.ports[port_id] : NULL;
}

static bool uart_driver_backend_is_initialized(const uart_runtime_port_t *port)
{
    return (port != NULL) && (port->ops != NULL) && port->ops->is_initialized(&port->backend);
}

static ring_buffer_t *uart_driver_rx_ring(uart_runtime_port_t *port)
{
    return ((port != NULL) && (port->ops != NULL)) ? port->ops->rx_ring(&port->backend) : NULL;
}

static ring_buffer_t *uart_driver_tx_ring(uart_runtime_port_t *port)
{
    return ((port != NULL) && (port->ops != NULL)) ? port->ops->tx_ring(&port->backend) : NULL;
}

/** @brief Revalidate a backend RX snapshot before the bridge commits its consumer sequence. */
static bool uart_driver_rx_snapshot_is_current(void *context, uint32_t consumer_sequence)
{
    uart_runtime_port_t *port = context;

    return (port != NULL) && (port->ops != NULL) &&
           port->ops->rx_snapshot_is_current(&port->backend, consumer_sequence);
}

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
           uart_control_mailbox_has_pending_port(&uart_driver_state.mailboxes[port_id], (uint32_t)port_id);
}

static const uart_worker_hooks_t uart_driver_worker_hooks = {
    .initialize = uart_driver_worker_initialize,
    .service_control = uart_driver_worker_service_control,
    .poll_io = uart_driver_poll_io,
    .heartbeat = &uart_driver_state.worker_heartbeat,
};

static void uart_driver_worker_initialize(void)
{
    hard_assert(flash_safe_execute_core_init());
    hw_uart_driver_enable_rx_dma_irq();
    pio_uart_driver_enable_rx_dma_irq();
}

static void uart_driver_worker_service_control(void)
{
    uart_driver_service_channel_lifecycle();
    uart_control_plane_service(&uart_driver_state.control_plane);
}

/** @brief Stop closed ports and start opened ports only on the UART worker core. */
static void uart_driver_service_channel_lifecycle(void)
{
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_port_id_t port_id = (uart_port_id_t)index;
        uart_runtime_port_t *port = &uart_driver_state.ports[index];
        bool reset_pending;
        bool lifecycle_ok;
        uint32_t save;

        save = spin_lock_blocking(uart_driver_state.status_lock);
        reset_pending = uart_driver_state.lifecycle[index].stop_pending;
        if (!reset_pending && !uart_driver_state.lifecycle[index].start_pending) {
            spin_unlock(uart_driver_state.status_lock, save);
            continue;
        }
        if (reset_pending && !uart_driver_state.lifecycle[index].usb_quiesced) {
            spin_unlock(uart_driver_state.status_lock, save);
            continue;
        }
        spin_unlock(uart_driver_state.status_lock, save);
        if (reset_pending) {
    #if PICO_UART_PROFILE_TIMING
            uart_driver_previous_service_us[index] = 0u;
    #endif
            if ((port->ops != NULL) && port->ops->is_initialized(&port->backend)) {
                uart_driver_begin_port_stats_update(port_id);
                port->ops->deinit(&port->backend);
                uart_driver_end_port_stats_update(port_id);
            }

            save = spin_lock_blocking(uart_driver_state.status_lock);
            uart_control_mailbox_reset(&uart_driver_state.mailboxes[index]);
            uart_driver_state.pending_controls[index] = (uart_control_pending_t){0};
            uart_driver_state.lifecycle[index].stop_pending = false;
            uart_driver_state.lifecycle[index].usb_quiesced = false;
            uart_driver_state.status_flags[index] &=
                (uint8_t)~(UART_DRIVER_PORT_STATUS_READY | UART_DRIVER_PORT_STATUS_INIT_FAILED);
            if (!uart_driver_state.soft_pending_controls[index]) {
                uart_driver_state.status_flags[index] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
            }
            spin_unlock(uart_driver_state.status_lock, save);
        }

        save = spin_lock_blocking(uart_driver_state.status_lock);
        if (!uart_driver_state.lifecycle[index].start_pending) {
            spin_unlock(uart_driver_state.status_lock, save);
            continue;
        }
        if (uart_driver_state.lifecycle[index].stop_pending) {
            spin_unlock(uart_driver_state.status_lock, save);
            continue;
        }
        uart_driver_state.lifecycle[index].start_pending = false;
        spin_unlock(uart_driver_state.status_lock, save);

        if ((port->ops == NULL) || (port->ops->restart == NULL)) {
            lifecycle_ok = false;
        } else {
            uart_driver_begin_port_stats_update(port_id);
            lifecycle_ok = uart_control_plane_start_port(&uart_driver_state.control_plane, port_id);
            if (lifecycle_ok) {
                port->ops->clear_rx_error_baseline(&port->backend);
            }
            uart_driver_end_port_stats_update(port_id);
        }

        save = spin_lock_blocking(uart_driver_state.status_lock);
        if (uart_driver_state.lifecycle[index].stop_pending || uart_driver_state.lifecycle[index].start_pending) {
            spin_unlock(uart_driver_state.status_lock, save);
            continue;
        } else if (lifecycle_ok) {
            uart_driver_state.status_flags[index] &= (uint8_t)~UART_DRIVER_PORT_STATUS_INIT_FAILED;
            uart_driver_state.status_flags[index] |= UART_DRIVER_PORT_STATUS_READY;
        } else {
            uart_driver_state.status_flags[index] |= UART_DRIVER_PORT_STATUS_INIT_FAILED;
            uart_driver_state.status_flags[index] &= (uint8_t)~UART_DRIVER_PORT_STATUS_READY;
        }
        spin_unlock(uart_driver_state.status_lock, save);
    }
}

static void uart_driver_bind_state_views(void)
{
    uart_driver_state.control_plane.ports = uart_driver_state.ports;
    uart_driver_state.control_plane.mailboxes = uart_driver_state.mailboxes;
    uart_driver_state.control_plane.pending_controls = uart_driver_state.pending_controls;
    uart_driver_state.control_plane.soft_pending_controls = uart_driver_state.soft_pending_controls;
    uart_driver_state.control_plane.control_generations = uart_driver_state.control_generations;
    uart_driver_state.control_plane.status_flags = uart_driver_state.status_flags;
    uart_driver_state.control_plane.status_lock = uart_driver_state.status_lock;
    uart_driver_state.control_plane.stats_sequence = uart_driver_state.stats_sequence;
    uart_driver_state.control_plane.poll_start_index = &uart_driver_state.poll_start_index;

}

static void uart_driver_worker_core_main(void)
{
    uart_worker_run(&uart_driver_worker_hooks);
}

static void uart_driver_load_board_config(void)
{
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        const uart_driver_port_info_t *board_port = &uart_board_ports[index];
        uart_runtime_port_t *port = &uart_driver_state.ports[index];

        port->info = *board_port;
        port->ops = uart_backend_ops_for_type(board_port->backend);
        if ((port->ops == NULL) || !port->ops->load_config(&port->backend, board_port)) {
            port->ops = NULL;
        } else if (board_port->backend == UART_DRIVER_BACKEND_HW) {
            port->backend.hw.rx_storage = uart_driver_rx_storage[index].hw;
        } else {
            port->backend.pio.rx_storage = uart_driver_rx_storage[index].pio;
        }
    }
}

static bool uart_driver_init_backends(void)
{
    bool init_ok = true;

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_runtime_port_t *port = &uart_driver_state.ports[index];
        bool port_ok = (port->ops != NULL) && port->ops->init(&port->backend);

        if (!port_ok) {
            init_ok = false;
            uart_driver_clear_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_READY);
            uart_driver_set_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_INIT_FAILED);
            continue;
        }

        uart_driver_clear_port_status_flag((uart_port_id_t)index,
                                           UART_DRIVER_PORT_STATUS_INIT_FAILED | UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
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

        if (uart_driver_channel_stop_is_quiesced((uart_port_id_t)index)) {
            continue;
        }

        if ((port->ops != NULL) && port->ops->is_initialized(&port->backend)) {
#if PICO_UART_PROFILE_TIMING
            uint32_t now_us = time_us_32();
            uint32_t previous_us = uart_driver_previous_service_us[index];
            uint32_t gap = now_us - previous_us;
            if ((previous_us != 0u) && (gap > uart_driver_max_service_gap_us[index])) {
                uart_driver_max_service_gap_us[index] = gap;
            }
            uart_driver_previous_service_us[index] = now_us;
#endif
            bool tx_launch_allowed =
                uart_control_plane_tx_launch_allowed(&uart_driver_state.control_plane, (uart_port_id_t)index);
            uart_driver_begin_port_stats_update((uart_port_id_t)index);
            port->ops->poll(&port->backend, tx_launch_allowed);
            uart_driver_end_port_stats_update((uart_port_id_t)index);
        }
#if PICO_UART_PROFILE_TIMING
        else {
            uart_driver_previous_service_us[index] = 0u;
        }
#endif
    }

    /* Paired with uart_control_plane_service(), which walked this same index
     * and left it unchanged. Advance once for the next worker step. */
    uart_driver_state.poll_start_index = (uart_driver_state.poll_start_index + 1u) % UART_PORT_COUNT;
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
            uart_driver_state.soft_pending_controls[index] = false;
            uart_driver_state.lifecycle[index] = (uart_driver_lifecycle_t){0};
            uart_driver_state.control_generations[index] = 0u;
            uart_driver_state.stats_sequence[index] = 0u;
            uart_driver_state.pending_controls[index] = (uart_control_pending_t){
                .line_coding = {
                    .baud_rate = uart_board_ports[index].baud_rate,
                    .data_bits = uart_board_ports[index].data_bits,
                    .stop_bits = uart_board_ports[index].stop_bits,
                    .parity = uart_board_ports[index].parity,
                },
            };
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
                port->ops->deinit(&port->backend);
                uart_driver_clear_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_READY);
            }
        }

        multicore_launch_core1(uart_driver_worker_core_main);
        uart_driver_state.worker_started = true;
    }

    return true;
}

size_t uart_driver_port_count(void)
{
    return UART_PORT_COUNT;
}

bool uart_driver_port_is_ready(uart_port_id_t port_id)
{
    return ((uart_driver_port_status(port_id) & UART_DRIVER_PORT_STATUS_READY) != 0u) &&
           uart_driver_backend_is_initialized(uart_driver_port(port_id));
}

bool uart_driver_request_channel_stop(uart_port_id_t port_id)
{
    uint32_t save;
    bool queued = false;

    if ((port_id >= UART_PORT_COUNT) || !uart_driver_state.worker_started ||
        (uart_driver_state.status_lock == NULL) || (uart_driver_state.ports[port_id].ops == NULL)) {
        return false;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_driver_state.lifecycle[port_id].start_pending = false;
    uart_driver_state.control_generations[port_id] += 1u;
    uart_driver_state.soft_pending_controls[port_id] = false;
    uart_driver_state.status_flags[port_id] &=
        (uint8_t)~(UART_DRIVER_PORT_STATUS_READY | UART_DRIVER_PORT_STATUS_INIT_FAILED |
                   UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
    uart_driver_state.status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    if (!uart_driver_state.lifecycle[port_id].stop_pending) {
        uart_driver_state.lifecycle[port_id].stop_pending = true;
        uart_driver_state.lifecycle[port_id].usb_quiesced = false;
        queued = true;
    }
    spin_unlock(uart_driver_state.status_lock, save);
    return queued;
}

bool uart_driver_request_channel_start(uart_port_id_t port_id)
{
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || !uart_driver_state.worker_started ||
        (uart_driver_state.status_lock == NULL) || (uart_driver_state.ports[port_id].ops == NULL)) {
        return false;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_driver_state.lifecycle[port_id].start_pending = true;
    spin_unlock(uart_driver_state.status_lock, save);
    return true;
}

bool uart_driver_channel_start_pending(uart_port_id_t port_id)
{
    uint32_t save;
    bool pending;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return false;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    pending = uart_driver_state.lifecycle[port_id].start_pending;
    spin_unlock(uart_driver_state.status_lock, save);
    return pending;
}

bool uart_driver_channel_stop_pending(uart_port_id_t port_id)
{
    uint32_t save;
    bool pending;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return false;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    pending = uart_driver_state.lifecycle[port_id].stop_pending;
    spin_unlock(uart_driver_state.status_lock, save);
    return pending;
}

void uart_driver_channel_stop_usb_quiesced(uart_port_id_t port_id)
{
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    if (uart_driver_state.lifecycle[port_id].stop_pending) {
        uart_driver_state.lifecycle[port_id].usb_quiesced = true;
    }
    spin_unlock(uart_driver_state.status_lock, save);
}

bool uart_driver_channel_stop_is_quiesced(uart_port_id_t port_id)
{
    uint32_t save;
    bool ready;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return false;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    ready = uart_driver_state.lifecycle[port_id].stop_pending && uart_driver_state.lifecycle[port_id].usb_quiesced;
    spin_unlock(uart_driver_state.status_lock, save);
    return ready;
}

size_t uart_driver_drain_rx(uart_port_id_t port_id, size_t capacity,
                            uint32_t (*writer)(void *context, const uint8_t *data, uint32_t length), void *context)
{
    uart_runtime_port_t *port = uart_driver_port(port_id);
    ring_buffer_t *rx_ring = uart_driver_rx_ring(port);

    if ((rx_ring == NULL) || !uart_driver_backend_is_initialized(port) || (writer == NULL)) {
        return 0u;
    }

    return uart_bridge_drain_rx(rx_ring, capacity, writer, context, uart_driver_rx_snapshot_is_current, port,
                                &uart_driver_state.stats_sequence[port_id]);
}

size_t uart_driver_recover_rx(uart_port_id_t port_id)
{
    uart_runtime_port_t *port = uart_driver_port(port_id);
    ring_buffer_t *rx_ring = uart_driver_rx_ring(port);

    if ((rx_ring == NULL) || !uart_driver_backend_is_initialized(port)) {
        return 0u;
    }

    return uart_bridge_recover_rx(rx_ring);
}

size_t uart_driver_fill_tx(uart_port_id_t port_id, size_t capacity,
                           uint32_t (*reader)(void *context, uint8_t *data, uint32_t length), void *context)
{
    uart_runtime_port_t *port = uart_driver_port(port_id);
    ring_buffer_t *tx_ring = uart_driver_tx_ring(port);

    if ((tx_ring == NULL) || !uart_driver_backend_is_initialized(port) || (reader == NULL) ||
        uart_driver_port_tx_is_blocked(port_id)) {
        return 0u;
    }

    return uart_bridge_fill_tx(tx_ring, capacity, reader, context);
}

bool uart_driver_line_coding_acceptable(uart_port_id_t port_id, const uart_driver_line_coding_t *line_coding)
{
    const uart_runtime_port_t *port = uart_driver_port(port_id);

    if ((port == NULL) || (uart_driver_state.status_lock == NULL) || !uart_line_coding_is_valid(line_coding)) {
        return false;
    }

    return (port->ops != NULL) && port->ops->line_coding_acceptable(line_coding);
}

bool uart_driver_queue_line_coding(uart_port_id_t port_id, const uart_driver_line_coding_t *line_coding,
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
        uart_driver_report_control_error(port_id, UART_CONTROL_ERROR_OTHER);
        return false;
    }

    port = &uart_driver_state.ports[port_id];
    tx_ring = port->ops->tx_ring(&port->backend);
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
    blocked =
        uart_control_tx_should_block(uart_driver_state.status_flags[port_id], UART_DRIVER_PORT_STATUS_CONTROL_PENDING);
    spin_unlock(uart_driver_state.status_lock, save);
    return blocked;
}

void uart_driver_report_control_error(uart_port_id_t port_id, uart_driver_control_error_t reason)
{
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_control_record_error(&uart_driver_state.ports[port_id], reason);
    uart_control_apply_reject_error(&uart_driver_state.control_generations[port_id],
                                    &uart_driver_state.status_flags[port_id], UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
    spin_unlock(uart_driver_state.status_lock, save);
}

void uart_driver_report_soft_pending_error(uart_port_id_t port_id, uint32_t control_generation,
                                           uart_driver_control_error_t reason)
{
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_control_record_error(&uart_driver_state.ports[port_id], reason);
    uart_driver_state.soft_pending_controls[port_id] = false;
    if (uart_control_completion_is_current(control_generation, uart_driver_state.control_generations[port_id])) {
        uart_driver_state.status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    }
    if (uart_control_pending_should_clear(false, uart_driver_mailbox_has_pending_port(port_id),
                                          uart_driver_state.pending_controls[port_id].pending)) {
        uart_driver_state.status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    }
    spin_unlock(uart_driver_state.status_lock, save);
}

bool uart_driver_control_history(uart_port_id_t port_id, uart_driver_control_history_t *history)
{
    if (port_id >= UART_PORT_COUNT || history == NULL || uart_driver_state.status_lock == NULL) {
        return false;
    }
    uint32_t save = spin_lock_blocking(uart_driver_state.status_lock);
    *history = uart_driver_state.ports[port_id].control_history;
    spin_unlock(uart_driver_state.status_lock, save);
    return true;
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

bool uart_driver_port_snapshot(uart_port_id_t port_id, uart_driver_port_info_t *info, uint8_t *status)
{
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (info == NULL) || (status == NULL) ||
        (uart_driver_state.status_lock == NULL)) {
        return false;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    *info = uart_driver_state.ports[port_id].info;
    *status = uart_driver_state.status_flags[port_id];
    spin_unlock(uart_driver_state.status_lock, save);
    return true;
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
    return uart_worker_heartbeat_is_fresh(heartbeat, &last_heartbeat, now_ms, &last_change_ms,
                                          UART_WORKER_HEARTBEAT_STALE_MS);
}

bool uart_driver_port_info(uart_port_id_t port_id, uart_driver_port_info_t *info)
{
    uart_runtime_port_t *port = uart_driver_port(port_id);
    uint32_t save;

    if ((port == NULL) || (info == NULL) || (uart_driver_state.status_lock == NULL)) {
        return false;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    *info = port->info;
    spin_unlock(uart_driver_state.status_lock, save);
    return true;
}

bool uart_driver_port_stats(uart_port_id_t port_id, uart_driver_port_stats_t *stats)
{
    uart_runtime_port_t *port = uart_driver_port(port_id);
    ring_buffer_t *rx_ring;
    ring_buffer_t *tx_ring;
    uart_backend_stats_t backend_stats;
    uint32_t first_sequence;
    uint32_t second_sequence;

    if ((stats == NULL) || port == NULL || port->ops == NULL) {
        return false;
    }

    rx_ring = uart_driver_rx_ring(port);
    tx_ring = uart_driver_tx_ring(port);
    if ((rx_ring == NULL) || (tx_ring == NULL)) {
        return false;
    }

    for (size_t attempt = 0u; attempt < UART_DRIVER_STATS_SNAPSHOT_ATTEMPTS; ++attempt) {
        first_sequence = uart_driver_state.stats_sequence[port_id];
        __dmb();
        if ((first_sequence & 1u) != 0u) {
            continue;
        }

        if (rx_ring->storage == NULL || tx_ring->storage == NULL) {
            return false;
        }
        uart_driver_port_stats_t candidate = {
            .tx_ring_high_watermark = (uint16_t)ring_buffer_high_watermark(tx_ring),
            .rx_ring_high_watermark = (uint16_t)ring_buffer_high_watermark(rx_ring),
            .tx_ring_overflow_count = (uint32_t)ring_buffer_overflow_count(tx_ring),
            .rx_ring_overflow_count = (uint32_t)ring_buffer_overflow_count(rx_ring),
            .rx_ring_pending_overflow_count = (uint32_t)ring_buffer_pending_overflow(rx_ring),
        };

        backend_stats = port->ops->stats(&port->backend);
        candidate.uart_tx_bytes = backend_stats.uart_tx_bytes;
        candidate.uart_rx_bytes = backend_stats.uart_rx_bytes;
        candidate.rx_error_count = backend_stats.rx_error_count;

        __dmb();
        second_sequence = uart_driver_state.stats_sequence[port_id];
        if ((first_sequence == second_sequence) && ((second_sequence & 1u) == 0u)) {
            *stats = candidate;
            return true;
        }
    }

    return false;
}

void uart_driver_reset_soft_pending(uart_port_id_t port_id)
{
    uint32_t save;

    if ((port_id >= UART_PORT_COUNT) || (uart_driver_state.status_lock == NULL)) {
        return;
    }

    save = spin_lock_blocking(uart_driver_state.status_lock);
    uart_driver_state.soft_pending_controls[port_id] = false;
    if (uart_control_pending_should_clear(false, uart_driver_mailbox_has_pending_port(port_id),
                                          uart_driver_state.pending_controls[port_id].pending)) {
        uart_driver_state.status_flags[port_id] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    }
    spin_unlock(uart_driver_state.status_lock, save);
}
