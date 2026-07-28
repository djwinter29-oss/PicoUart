/**
 * @file uart_driver.c
 * @brief Logical UART port table for the PicoUart firmware.
 */

#include "uart/uart_driver.h"

#include "config/uart_board.h"
#include "uart/control_pending.h"
#include "uart/hw/driver.h"
#include "uart/line_coding.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "uart/pio/internal.h"
#include "uart/ring_buffer/ring_buffer.h"

/** @brief Default startup baud rate applied to all logical UART ports. */
#define UART_DRIVER_DEFAULT_BAUD_RATE 115200u
/** @brief Maximum time the worker may defer applying a line-coding change. */
#define UART_DRIVER_CONTROL_APPLY_TIMEOUT_MS 1000u
/** @brief Maximum attempts to acquire a coherent worker-owned telemetry snapshot. */
#define UART_DRIVER_PORT_STATS_SNAPSHOT_ATTEMPTS 3u

/**
 * @brief Commands accepted by the cross-core UART control mailbox.
 */
typedef enum {
    UART_DRIVER_MAILBOX_COMMAND_NONE = 0, /**< No pending command. */
    UART_DRIVER_MAILBOX_COMMAND_SET_LINE_CODING, /**< Apply a line-coding change on core 1. */
} uart_driver_mailbox_command_t;

/**
 * @brief Single-slot mailbox shared between the USB core and the UART worker core.
 */
typedef struct {
    volatile uint32_t request_sequence; /**< Monotonic request sequence published by core 0. */
    volatile uint32_t response_sequence; /**< Latest completed request sequence published by core 1. */
    volatile uart_driver_mailbox_command_t command; /**< Pending mailbox command. */
    volatile uint32_t port_id; /**< Port argument used by parameterized commands. */
    volatile uint32_t control_generation; /**< Host request generation associated with the command. */
    volatile uart_driver_line_coding_t line_coding; /**< Pending line-coding payload. */
    volatile uint32_t result_port_id; /**< Port associated with the command result. */
    volatile uart_driver_command_status_t result_status; /**< Command result written by core 1. */
} uart_driver_mailbox_t;

/**
 * @brief Runtime storage for one logical UART port.
 */
typedef struct {
    uart_driver_port_info_t info; /**< Public metadata for the logical port. */
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
    absolute_time_t deadline; /**< Absolute time when a deferred apply must succeed or fail. */
    uart_driver_line_coding_t line_coding; /**< Latest requested line-coding payload. */
} uart_driver_pending_control_t;

/** @brief Runtime state for the 6-port bridge, initialized from the board map. */
static uart_driver_port_t uart_ports[UART_PORT_COUNT];

/** @brief Core-to-core mailbox used for UART control operations. */
static uart_driver_mailbox_t uart_driver_mailbox;
/** @brief True after the dedicated UART worker core has been launched. */
static bool uart_driver_worker_started;
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
static uart_driver_command_status_t uart_driver_init_backends(uint32_t *result_port_id);
static void uart_driver_load_board_config(void);
static void uart_driver_poll_backends(void);
static uart_parity_t uart_driver_hw_parity(uart_driver_parity_t parity);
static bool uart_driver_line_coding_matches_current(const uart_driver_port_t *port,
                                                    const uart_driver_line_coding_t *line_coding);
static uart_driver_command_status_t uart_driver_set_line_coding_local(
    uart_port_id_t port_id,
    const uart_driver_line_coding_t *line_coding,
    uint32_t control_generation,
    uint32_t *result_port_id);
static void uart_driver_service_pending_control(uart_port_id_t port_id, uart_driver_port_t *port);
static void uart_driver_set_worker_control_pending(uart_port_id_t port_id, uint32_t control_generation);
static void uart_driver_finish_worker_control(uart_port_id_t port_id,
                                              uint32_t control_generation,
                                              bool success);
static void uart_driver_finish_mailbox_control(uart_port_id_t port_id,
                                               uint32_t control_generation,
                                               bool success);
static bool uart_driver_mailbox_has_pending_port(uart_port_id_t port_id);

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

/**
 * @brief Halt after a HardFault so the debugger can inspect the fault.
 */
void isr_hardfault(void)
{
    while (true) {
        tight_loop_contents();
    }
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
    hw_uart_driver_enable_rx_dma_irq();
    pio_uart_driver_enable_rx_dma_irq();

    while (true) {
        uint32_t request_sequence = uart_driver_mailbox.request_sequence;

        if (request_sequence != uart_driver_mailbox.response_sequence) {
            uart_driver_command_status_t status = UART_DRIVER_COMMAND_STATUS_UNSUPPORTED;
            uint32_t result_port_id = UART_PORT_COUNT;
            uint32_t control_generation;
            uart_driver_line_coding_t line_coding;

            __dmb();
            control_generation = uart_driver_mailbox.control_generation;
            if (uart_driver_mailbox.command == UART_DRIVER_MAILBOX_COMMAND_SET_LINE_CODING) {
                line_coding = uart_driver_mailbox.line_coding;
                status = uart_driver_set_line_coding_local((uart_port_id_t)uart_driver_mailbox.port_id,
                                                           &line_coding,
                                                           control_generation,
                                                           &result_port_id);
            }

            uart_driver_mailbox.result_port_id = result_port_id;
            uart_driver_mailbox.result_status = status;
            __dmb();
            uart_driver_mailbox.response_sequence = request_sequence;
        }

        uart_driver_poll_backends();
        uart_driver_poll_hardware();
        uart_driver_poll_pio();
        tight_loop_contents();
    }
}

static uart_parity_t uart_driver_hw_parity(uart_driver_parity_t parity)
{
    if (parity == UART_DRIVER_PARITY_ODD) {
        return UART_PARITY_ODD;
    }

    if (parity == UART_DRIVER_PARITY_EVEN) {
        return UART_PARITY_EVEN;
    }

    return UART_PARITY_NONE;
}

static bool uart_driver_line_coding_matches_current(const uart_driver_port_t *port,
                                                    const uart_driver_line_coding_t *line_coding)
{
    if ((port == NULL) || (line_coding == NULL)) {
        return false;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        return (port->backend.hw.config.baud_rate == line_coding->baud_rate) &&
               (port->backend.hw.config.data_bits == line_coding->data_bits) &&
               (port->backend.hw.config.stop_bits == line_coding->stop_bits) &&
               (port->backend.hw.config.parity == uart_driver_hw_parity(line_coding->parity));
    }

    if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        return (port->backend.pio.config.baud_rate == line_coding->baud_rate) &&
               (line_coding->data_bits == 8u) &&
               (line_coding->stop_bits == 1u) &&
               (line_coding->parity == UART_DRIVER_PARITY_NONE);
    }

    return false;
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
    if (port == NULL) {
        return NULL;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        return &port->backend.hw.rx_ring;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        return &port->backend.pio.rx_ring;
    }

    return NULL;
}

static ring_buffer_t *uart_driver_tx_ring_mutable(uart_driver_port_t *port)
{
    if (port == NULL) {
        return NULL;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        return &port->backend.hw.tx_ring;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        return &port->backend.pio.tx_ring;
    }

    return NULL;
}

size_t uart_driver_port_count(void)
{
    return UART_PORT_COUNT;
}

static uart_driver_command_status_t uart_driver_init_backends(uint32_t *result_port_id)
{
    bool init_ok = true;

    if (result_port_id != NULL) {
        *result_port_id = UART_PORT_COUNT;
    }

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_driver_port_t *port = &uart_ports[index];

        if (port->info.backend == UART_DRIVER_BACKEND_HW) {
            if (!port->backend.hw.initialized) {
                if (!hw_uart_driver_init(&port->backend.hw)) {
                    init_ok = false;
                    uart_driver_clear_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_READY);
                    uart_driver_set_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_INIT_FAILED);
                    if ((result_port_id != NULL) && (*result_port_id == UART_PORT_COUNT)) {
                        *result_port_id = (uint32_t)index;
                    }
                } else {
                    uart_driver_clear_port_status_flag((uart_port_id_t)index,
                                                       UART_DRIVER_PORT_STATUS_INIT_FAILED |
                                                           UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
                    uart_driver_set_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_READY);
                }
            } else {
                uart_driver_clear_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_INIT_FAILED);
                uart_driver_set_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_READY);
            }
            continue;
        }

        if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
            if (!port->backend.pio.initialized) {
                if (!pio_uart_driver_init(&port->backend.pio)) {
                    init_ok = false;
                    uart_driver_clear_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_READY);
                    uart_driver_set_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_INIT_FAILED);
                    if ((result_port_id != NULL) && (*result_port_id == UART_PORT_COUNT)) {
                        *result_port_id = (uint32_t)index;
                    }
                } else {
                    uart_driver_clear_port_status_flag((uart_port_id_t)index,
                                                       UART_DRIVER_PORT_STATUS_INIT_FAILED |
                                                           UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
                    uart_driver_set_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_READY);
                }
            } else {
                uart_driver_clear_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_INIT_FAILED);
                uart_driver_set_port_status_flag((uart_port_id_t)index, UART_DRIVER_PORT_STATUS_READY);
            }
        }
    }

    return init_ok ? UART_DRIVER_COMMAND_STATUS_OK : UART_DRIVER_COMMAND_STATUS_INIT_FAILED;
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

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        applied = hw_uart_driver_set_line_format(&port->backend.hw,
                                                 pending_control->line_coding.baud_rate,
                                                 pending_control->line_coding.data_bits,
                                                 pending_control->line_coding.stop_bits,
                                                 uart_driver_hw_parity(pending_control->line_coding.parity));
    } else if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        applied = pio_uart_driver_set_baud_rate(&port->backend.pio,
                                                pending_control->line_coding.baud_rate);
    } else {
        uart_driver_finish_worker_control(port_id, pending_control->control_generation, false);
        return;
    }

    if (!applied) {
        if (!time_reached(pending_control->deadline)) {
            return;
        }

        /* Continuous RX/TX activity can defer a PIO/HW reconfigure forever;
         * fail the request so USB ingress is not paused indefinitely. */
        uart_driver_finish_worker_control(port_id, pending_control->control_generation, false);
        return;
    }

    port->info.baud_rate = pending_control->line_coding.baud_rate;
    __dmb();
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
            uart_driver_pending_controls[index].line_coding.baud_rate = UART_DRIVER_DEFAULT_BAUD_RATE;
            uart_driver_pending_controls[index].line_coding.data_bits = 8u;
            uart_driver_pending_controls[index].line_coding.stop_bits = 1u;
            uart_driver_pending_controls[index].line_coding.parity = UART_DRIVER_PARITY_NONE;
        }

        uart_driver_mailbox.request_sequence = 0u;
        uart_driver_mailbox.response_sequence = 0u;
        uart_driver_mailbox.command = UART_DRIVER_MAILBOX_COMMAND_NONE;
        uart_driver_mailbox.port_id = 0u;
        uart_driver_mailbox.control_generation = 0u;
        uart_driver_mailbox.line_coding.baud_rate = 0u;
        uart_driver_mailbox.line_coding.data_bits = 8u;
        uart_driver_mailbox.line_coding.stop_bits = 1u;
        uart_driver_mailbox.line_coding.parity = UART_DRIVER_PARITY_NONE;
        uart_driver_mailbox.result_port_id = UART_PORT_COUNT;
        uart_driver_mailbox.result_status = UART_DRIVER_COMMAND_STATUS_WORKER_NOT_STARTED;
        uart_driver_poll_start_index = 0u;

        if (uart_driver_init_backends(NULL) != UART_DRIVER_COMMAND_STATUS_OK) {
            return false;
        }

        multicore_launch_core1(uart_driver_worker_core_main);
        uart_driver_worker_started = true;
    }

    return true;
}

void uart_driver_poll(void)
{
    /* ponytail: UART live service now runs on core 1 so core 0 only owns USB.
     * Ceiling: core-0 callers no longer advance UART state directly.
     * Acceptable now because the dedicated worker loop polls continuously.
     * Upgrade path: remove this shim after callers no longer reference it.
     */
}

bool uart_driver_port_is_ready(uart_port_id_t port_id)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);

    if (port == NULL) {
        return false;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        return port->backend.hw.initialized;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        return port->backend.pio.initialized;
    }

    return false;
}

void uart_driver_poll_hardware(void)
{
    for (size_t index = UART_PORT_0; index <= UART_PORT_1; ++index) {
        uart_driver_port_t *port = &uart_ports[index];

        if (port->backend.hw.initialized) {
            uart_driver_begin_port_stats_update((uart_port_id_t)index);
            hw_uart_driver_poll(&port->backend.hw);
            uart_driver_end_port_stats_update((uart_port_id_t)index);
        }
    }
}

void uart_driver_poll_pio(void)
{
    for (size_t index = UART_PORT_2; index < UART_PORT_COUNT; ++index) {
        uart_driver_port_t *port = &uart_ports[index];

        if (port->backend.pio.initialized) {
            uart_driver_begin_port_stats_update((uart_port_id_t)index);
            pio_uart_driver_poll(&port->backend.pio);
            uart_driver_end_port_stats_update((uart_port_id_t)index);
        }
    }
}

size_t uart_driver_drain_rx(uart_port_id_t port_id,
                            size_t capacity,
                            uint32_t (*writer)(void *context, const uint8_t *data, uint32_t length),
                            void *context)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    ring_buffer_t *rx_ring;
    size_t total_written = 0u;

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

        written = writer(context, span.data, offered);
        if (written == 0u) {
            break;
        }

        if ((written > offered) || !ring_buffer_commit_consumed(rx_ring, written)) {
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
        ((uart_driver_port_status(port_id) & UART_DRIVER_PORT_STATUS_CONTROL_PENDING) != 0u)) {
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

static uart_driver_command_status_t uart_driver_set_line_coding_local(
    uart_port_id_t port_id,
    const uart_driver_line_coding_t *line_coding,
    uint32_t control_generation,
    uint32_t *result_port_id)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    /* Preserve unread RX bytes across a line-format change by restarting DMA at
     * the live producer index instead of resetting the shared RX ring.
     */
    uart_driver_pending_control_t *pending_control;

    if (result_port_id != NULL) {
        *result_port_id = (uint32_t)port_id;
    }

    if (port == NULL) {
        return UART_DRIVER_COMMAND_STATUS_INVALID_PORT;
    }

    pending_control = &uart_driver_pending_controls[port_id];

    if (!uart_line_coding_is_valid(line_coding)) {
        uart_driver_finish_mailbox_control(port_id, control_generation, false);
        return UART_DRIVER_COMMAND_STATUS_INVALID_ARGUMENT;
    }

    if (uart_driver_line_coding_matches_current(port, line_coding)) {
        if (pending_control->pending) {
            /* The newest request keeps the current format, so cancel the older deferred change. */
            uart_driver_finish_worker_control(port_id, pending_control->control_generation, true);
            uart_driver_finish_mailbox_control(port_id, control_generation, true);
        } else {
            uart_driver_finish_mailbox_control(port_id, control_generation, true);
        }
        return UART_DRIVER_COMMAND_STATUS_OK;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        pending_control->line_coding = *line_coding;
    } else if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        if (!uart_line_coding_pio_supported(line_coding, clock_get_hz(clk_sys))) {
            uart_driver_finish_mailbox_control(port_id, control_generation, false);
            return UART_DRIVER_COMMAND_STATUS_BACKEND_REJECTED;
        }

        pending_control->line_coding = *line_coding;
    } else {
        uart_driver_finish_mailbox_control(port_id, control_generation, false);
        return UART_DRIVER_COMMAND_STATUS_UNSUPPORTED;
    }

    pending_control->deadline = make_timeout_time_ms(UART_DRIVER_CONTROL_APPLY_TIMEOUT_MS);
    uart_driver_set_worker_control_pending(port_id, control_generation);
    return UART_DRIVER_COMMAND_STATUS_QUEUED;
}

bool uart_driver_line_coding_acceptable(uart_port_id_t port_id,
                                        const uart_driver_line_coding_t *line_coding)
{
    const uart_driver_port_info_t *port_info = uart_driver_port_info(port_id);

    if ((port_info == NULL) || !uart_line_coding_is_valid(line_coding)) {
        return false;
    }

    if (port_info->backend == UART_DRIVER_BACKEND_PIO) {
        return uart_line_coding_pio_supported(line_coding, clock_get_hz(clk_sys));
    }

    return port_info->backend == UART_DRIVER_BACKEND_HW;
}

bool uart_driver_queue_line_coding(uart_port_id_t port_id,
                                   const uart_driver_line_coding_t *line_coding,
                                   uint32_t control_generation)
{
    uint32_t request_sequence;

    if (!uart_driver_worker_started) {
        return false;
    }

    if (!uart_driver_line_coding_acceptable(port_id, line_coding)) {
        uart_driver_report_control_error(port_id);
        return false;
    }

    if (uart_driver_mailbox.request_sequence != uart_driver_mailbox.response_sequence) {
        return false;
    }

    request_sequence = uart_driver_mailbox.request_sequence + 1u;
    uint32_t save = spin_lock_blocking(uart_driver_status_lock);

    uart_driver_soft_pending_controls[port_id] = false;
    uart_driver_port_status_flags[port_id] |= UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    uart_driver_mailbox.port_id = (uint32_t)port_id;
    uart_driver_mailbox.control_generation = control_generation;
    uart_driver_mailbox.line_coding = *line_coding;
    uart_driver_mailbox.command = UART_DRIVER_MAILBOX_COMMAND_SET_LINE_CODING;
    __dmb();
    uart_driver_mailbox.request_sequence = request_sequence;
    spin_unlock(uart_driver_status_lock, save);
    return true;
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

const uart_driver_port_info_t *uart_driver_port_info(uart_port_id_t port_id)
{
    if (port_id >= UART_PORT_COUNT) {
        return NULL;
    }

    return &uart_ports[port_id].info;
}

bool uart_driver_port_stats(uart_port_id_t port_id, uart_driver_port_stats_t *stats)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
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

        if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
            stats->controller_tx_bytes = (uint32_t)(port->backend.pio.tx_polled_bytes +
                                                    port->backend.pio.tx_dma_bytes);
            stats->controller_rx_bytes = port->backend.pio.controller_rx_bytes;
            stats->rx_error_count = port->backend.pio.rx_error_count;
        } else if (port->info.backend == UART_DRIVER_BACKEND_HW) {
            stats->controller_tx_bytes = port->backend.hw.controller_tx_bytes;
            stats->controller_rx_bytes = port->backend.hw.controller_rx_bytes;
            stats->rx_error_count = port->backend.hw.rx_error_count;
        }

        __dmb();
        second_sequence = uart_driver_port_stats_sequence[port_id];
        if ((first_sequence == second_sequence) && ((second_sequence & 1u) == 0u)) {
            return true;
        }
    }

    return false;
}

bool uart_driver_validate_topology(void)
{
    size_t hw_count = 0u;
    size_t pio_count = 0u;

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        const uart_board_port_config_t *board = &uart_board_ports[index];

        if (board->info.id != (uart_port_id_t)index) {
            return false;
        }

        if (board->info.backend == UART_DRIVER_BACKEND_HW) {
            if ((board->info.tx_pin != board->backend.hw.tx_pin) ||
                (board->info.rx_pin != board->backend.hw.rx_pin) ||
                (board->info.baud_rate != board->backend.hw.baud_rate)) {
                return false;
            }
            hw_count += 1u;
            continue;
        }

        if (board->info.backend == UART_DRIVER_BACKEND_PIO) {
            if ((board->info.tx_pin != board->backend.pio.tx_pin) ||
                (board->info.rx_pin != board->backend.pio.rx_pin) ||
                (board->info.baud_rate != board->backend.pio.baud_rate)) {
                return false;
            }
            pio_count += 1u;
            continue;
        }

        return false;
    }

    return (hw_count == 2u) && (pio_count == 4u);
}