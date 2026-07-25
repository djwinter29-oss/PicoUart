/**
 * @file uart_driver.c
 * @brief Logical UART port table for the PicoUart firmware.
 */

#include "driver/uart_driver.h"

#include "driver/hw_uart_driver.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "pio/pio_uart_driver_internal.h"
#include "ring_buffer/ring_buffer.h"

/** @brief Default startup baud rate applied to all logical UART ports. */
#define UART_DRIVER_DEFAULT_BAUD_RATE 115200u
/** @brief Maximum time core 0 waits for a UART worker mailbox response. */
#define UART_DRIVER_MAILBOX_TIMEOUT_MS 20u

/**
 * @brief Commands accepted by the cross-core UART control mailbox.
 */
typedef enum {
    UART_DRIVER_MAILBOX_COMMAND_NONE = 0, /**< No pending command. */
    UART_DRIVER_MAILBOX_COMMAND_INIT, /**< Initialize all UART backends on core 1. */
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
    uart_driver_line_coding_t line_coding; /**< Latest requested line-coding payload. */
} uart_driver_pending_control_t;

/** @brief Static logical UART port table for the 6-port bridge. */
static uart_driver_port_t uart_ports[UART_PORT_COUNT] = {
    {
        .info = {UART_PORT_0, UART_DRIVER_BACKEND_HW, UART_DRIVER_DEFAULT_BAUD_RATE, 0u, 1u},
        .backend.hw = {
            .config = {uart0, UART_DRIVER_DEFAULT_BAUD_RATE, 0u, 1u, 8u, 1u, UART_PARITY_NONE},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_1, UART_DRIVER_BACKEND_HW, UART_DRIVER_DEFAULT_BAUD_RATE, 4u, 5u},
        .backend.hw = {
            .config = {uart1, UART_DRIVER_DEFAULT_BAUD_RATE, 4u, 5u, 8u, 1u, UART_PARITY_NONE},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_2, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 8u, 9u},
        .backend.pio = {
            .config = {pio0,
                       0u,
                       1u,
                       UART_DRIVER_DEFAULT_BAUD_RATE,
                       8u,
                       9u,
                       PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP,
                       PIO_UART_DRIVER_DEFAULT_TX_DMA_START_THRESHOLD},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_3, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 12u, 13u},
        .backend.pio = {
            .config = {pio0,
                       2u,
                       3u,
                       UART_DRIVER_DEFAULT_BAUD_RATE,
                       12u,
                       13u,
                       PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP,
                       PIO_UART_DRIVER_DEFAULT_TX_DMA_START_THRESHOLD},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_4, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 16u, 17u},
        .backend.pio = {
            .config = {pio1,
                       0u,
                       1u,
                       UART_DRIVER_DEFAULT_BAUD_RATE,
                       16u,
                       17u,
                       PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP,
                       PIO_UART_DRIVER_DEFAULT_TX_DMA_START_THRESHOLD},
            .initialized = false,
        },
    },
    {
        .info = {UART_PORT_5, UART_DRIVER_BACKEND_PIO, UART_DRIVER_DEFAULT_BAUD_RATE, 20u, 21u},
        .backend.pio = {
            .config = {pio1,
                       2u,
                       3u,
                       UART_DRIVER_DEFAULT_BAUD_RATE,
                       20u,
                       21u,
                       PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP,
                       PIO_UART_DRIVER_DEFAULT_TX_DMA_START_THRESHOLD},
            .initialized = false,
        },
    },
};

/** @brief Core-to-core mailbox used for UART control operations. */
static uart_driver_mailbox_t uart_driver_mailbox;
/** @brief True after the dedicated UART worker core has been launched. */
static bool uart_driver_worker_started;
/** @brief Latest completed control status visible on core 0. */
static volatile uart_driver_command_status_t uart_driver_last_status = UART_DRIVER_COMMAND_STATUS_WORKER_NOT_STARTED;
/** @brief Logical port associated with the latest control status. */
static volatile uart_port_id_t uart_driver_last_status_port = UART_PORT_COUNT;
/** @brief Per-port status flags for monitoring and HID reporting. */
static volatile uint8_t uart_driver_port_status_flags[UART_PORT_COUNT];
/** @brief Deferred line-coding requests owned by the worker core. */
static uart_driver_pending_control_t uart_driver_pending_controls[UART_PORT_COUNT];
/** @brief Worker-loop port index that starts the next backend poll sweep. */
static size_t uart_driver_poll_start_index;
/** @brief Logical port currently being serviced by the worker, or count between passes. */
static volatile uart_port_id_t uart_driver_worker_poll_port_id = UART_PORT_COUNT;
/** @brief Incremented after each completed worker backend poll sweep. */
static volatile uint8_t uart_driver_worker_heartbeat_value;
/** @brief Core that most recently entered HardFault, or zero when none has occurred. */
static volatile uint8_t uart_driver_hardfault_core_value;

static uart_driver_command_status_t uart_driver_init_backends(uint32_t *result_port_id);
static void uart_driver_poll_backends(void);
static bool uart_driver_line_coding_is_valid(const uart_driver_line_coding_t *line_coding);
static bool uart_driver_current_line_coding(const uart_driver_port_t *port,
                                            uart_driver_line_coding_t *line_coding);
static uart_parity_t uart_driver_hw_parity(uart_driver_parity_t parity);
static bool uart_driver_pio_line_coding_supported(const uart_driver_line_coding_t *line_coding);
static uart_driver_command_status_t uart_driver_set_line_coding_local(
    uart_port_id_t port_id,
    const uart_driver_line_coding_t *line_coding,
    uint32_t *result_port_id);
static void uart_driver_service_pending_control(uart_port_id_t port_id, uart_driver_port_t *port);

/**
 * @brief Preserve the faulting core for nonintrusive HID diagnostics.
 */
void isr_hardfault(void)
{
    uart_driver_hardfault_core_value = (uint8_t)(get_core_num() + 1u);
    while (true) {
        tight_loop_contents();
    }
}

static void uart_driver_set_last_status(uart_driver_command_status_t status, uart_port_id_t port_id)
{
    uart_driver_last_status = status;
    uart_driver_last_status_port = port_id;
}

static void uart_driver_set_port_status_flag(uart_port_id_t port_id, uint8_t flag)
{
    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    uart_driver_port_status_flags[port_id] |= flag;
}

static void uart_driver_clear_port_status_flag(uart_port_id_t port_id, uint8_t flag)
{
    if (port_id >= UART_PORT_COUNT) {
        return;
    }

    uart_driver_port_status_flags[port_id] &= (uint8_t)~flag;
}

static void uart_driver_worker_core_main(void)
{
    while (true) {
        uint32_t request_sequence = uart_driver_mailbox.request_sequence;

        if (request_sequence != uart_driver_mailbox.response_sequence) {
            uart_driver_command_status_t status = UART_DRIVER_COMMAND_STATUS_UNSUPPORTED;
            uint32_t result_port_id = UART_PORT_COUNT;

            __dmb();
            if (uart_driver_mailbox.command == UART_DRIVER_MAILBOX_COMMAND_INIT) {
                status = uart_driver_init_backends(&result_port_id);
            } else if (uart_driver_mailbox.command == UART_DRIVER_MAILBOX_COMMAND_SET_LINE_CODING) {
                status = uart_driver_set_line_coding_local((uart_port_id_t)uart_driver_mailbox.port_id,
                                                           &uart_driver_mailbox.line_coding,
                                                           &result_port_id);
            }

            uart_driver_mailbox.result_port_id = result_port_id;
            uart_driver_mailbox.result_status = status;
            __dmb();
            uart_driver_mailbox.response_sequence = request_sequence;
        }

        uart_driver_poll_backends();
        uart_driver_worker_heartbeat_value += 1u;
        tight_loop_contents();
    }
}

static bool uart_driver_mailbox_call(uart_driver_mailbox_command_t command,
                                     uint32_t port_id,
                                     const uart_driver_line_coding_t *line_coding)
{
    absolute_time_t deadline = make_timeout_time_ms(UART_DRIVER_MAILBOX_TIMEOUT_MS);
    uint32_t request_sequence;

    while (uart_driver_mailbox.request_sequence != uart_driver_mailbox.response_sequence) {
        if (time_reached(deadline)) {
            uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_TIMEOUT, (uart_port_id_t)port_id);
            return false;
        }
        tight_loop_contents();
    }

    request_sequence = uart_driver_mailbox.request_sequence + 1u;
    uart_driver_mailbox.port_id = port_id;
    if (line_coding != NULL) {
        uart_driver_mailbox.line_coding = *line_coding;
    }
    uart_driver_mailbox.command = command;
    __dmb();
    uart_driver_mailbox.request_sequence = request_sequence;

    while (uart_driver_mailbox.response_sequence != request_sequence) {
        if (time_reached(deadline)) {
            uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_TIMEOUT, (uart_port_id_t)port_id);
            return false;
        }
        tight_loop_contents();
    }

    __dmb();
    uart_driver_set_last_status(uart_driver_mailbox.result_status,
                                (uart_port_id_t)uart_driver_mailbox.result_port_id);
    uart_driver_mailbox.command = UART_DRIVER_MAILBOX_COMMAND_NONE;
    return (uart_driver_mailbox.result_status == UART_DRIVER_COMMAND_STATUS_OK) ||
           (uart_driver_mailbox.result_status == UART_DRIVER_COMMAND_STATUS_QUEUED);
}

static bool uart_driver_line_coding_is_valid(const uart_driver_line_coding_t *line_coding)
{
    if ((line_coding == NULL) || (line_coding->baud_rate == 0u)) {
        return false;
    }

    if ((line_coding->data_bits < 5u) || (line_coding->data_bits > 8u)) {
        return false;
    }

    if ((line_coding->stop_bits != 1u) && (line_coding->stop_bits != 2u)) {
        return false;
    }

    return (line_coding->parity == UART_DRIVER_PARITY_NONE) ||
           (line_coding->parity == UART_DRIVER_PARITY_ODD) ||
           (line_coding->parity == UART_DRIVER_PARITY_EVEN);
}

static bool uart_driver_current_line_coding(const uart_driver_port_t *port,
                                            uart_driver_line_coding_t *line_coding)
{
    if ((port == NULL) || (line_coding == NULL)) {
        return false;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        line_coding->baud_rate = port->backend.hw.config.baud_rate;
        line_coding->data_bits = port->backend.hw.config.data_bits;
        line_coding->stop_bits = port->backend.hw.config.stop_bits;
        line_coding->parity = (port->backend.hw.config.parity == UART_PARITY_ODD) ?
                                  UART_DRIVER_PARITY_ODD :
                                  ((port->backend.hw.config.parity == UART_PARITY_EVEN) ?
                                       UART_DRIVER_PARITY_EVEN :
                                       UART_DRIVER_PARITY_NONE);
        return true;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        line_coding->baud_rate = port->backend.pio.config.baud_rate;
        line_coding->data_bits = 8u;
        line_coding->stop_bits = 1u;
        line_coding->parity = UART_DRIVER_PARITY_NONE;
        return true;
    }

    return false;
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

static bool uart_driver_pio_line_coding_supported(const uart_driver_line_coding_t *line_coding)
{
    if (!uart_driver_line_coding_is_valid(line_coding)) {
        return false;
    }

    return (line_coding->data_bits == 8u) &&
           (line_coding->stop_bits == 1u) &&
           (line_coding->parity == UART_DRIVER_PARITY_NONE);
}

static uart_driver_port_t *uart_driver_port_mutable(uart_port_id_t port_id)
{
    if (port_id >= UART_PORT_COUNT) {
        return NULL;
    }

    return &uart_ports[port_id];
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

        uart_driver_worker_poll_port_id = (uart_port_id_t)index;

        uart_driver_service_pending_control((uart_port_id_t)index, port);
    }

    uart_driver_worker_poll_port_id = UART_PORT_COUNT;
    uart_driver_poll_start_index = (uart_driver_poll_start_index + 1u) % UART_PORT_COUNT;
}

static void uart_driver_service_pending_control(uart_port_id_t port_id, uart_driver_port_t *port)
{
    uart_driver_pending_control_t *pending_control;

    if ((port == NULL) || (port_id >= UART_PORT_COUNT)) {
        return;
    }

    pending_control = &uart_driver_pending_controls[port_id];
    if (!pending_control->pending) {
        return;
    }

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        if (!hw_uart_driver_set_line_format(&port->backend.hw,
                                            pending_control->line_coding.baud_rate,
                                            pending_control->line_coding.data_bits,
                                            pending_control->line_coding.stop_bits,
                                            uart_driver_hw_parity(pending_control->line_coding.parity))) {
            return;
        }
    } else if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        if (!pio_uart_driver_set_baud_rate(&port->backend.pio,
                                           pending_control->line_coding.baud_rate)) {
            return;
        }
    } else {
        pending_control->pending = false;
        uart_driver_clear_port_status_flag(port_id, UART_DRIVER_PORT_STATUS_CONTROL_PENDING);
        uart_driver_set_port_status_flag(port_id, UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
        uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_UNSUPPORTED, port_id);
        return;
    }

    pending_control->pending = false;
    uart_driver_clear_port_status_flag(port_id,
                                       UART_DRIVER_PORT_STATUS_CONTROL_PENDING |
                                           UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
    port->info.baud_rate = pending_control->line_coding.baud_rate;
    uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_OK, port_id);
}

bool uart_driver_init(void)
{
    if (!uart_driver_worker_started) {
        for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
            uart_driver_port_status_flags[index] = 0u;
            uart_driver_pending_controls[index].pending = false;
            uart_driver_pending_controls[index].line_coding.baud_rate = UART_DRIVER_DEFAULT_BAUD_RATE;
            uart_driver_pending_controls[index].line_coding.data_bits = 8u;
            uart_driver_pending_controls[index].line_coding.stop_bits = 1u;
            uart_driver_pending_controls[index].line_coding.parity = UART_DRIVER_PARITY_NONE;
        }
        uart_driver_mailbox.request_sequence = 0u;
        uart_driver_mailbox.response_sequence = 0u;
        uart_driver_mailbox.command = UART_DRIVER_MAILBOX_COMMAND_NONE;
        uart_driver_mailbox.port_id = 0u;
        uart_driver_mailbox.line_coding.baud_rate = 0u;
        uart_driver_mailbox.line_coding.data_bits = 8u;
        uart_driver_mailbox.line_coding.stop_bits = 1u;
        uart_driver_mailbox.line_coding.parity = UART_DRIVER_PARITY_NONE;
        uart_driver_mailbox.result_port_id = UART_PORT_COUNT;
        uart_driver_mailbox.result_status = UART_DRIVER_COMMAND_STATUS_WORKER_NOT_STARTED;
        uart_driver_poll_start_index = 0u;
        uart_driver_worker_poll_port_id = UART_PORT_COUNT;
        uart_driver_worker_heartbeat_value = 0u;
        uart_driver_hardfault_core_value = 0u;
        multicore_reset_core1();
        multicore_launch_core1(uart_driver_worker_core_main);
        uart_driver_worker_started = true;
    }

    return uart_driver_mailbox_call(UART_DRIVER_MAILBOX_COMMAND_INIT, 0u, NULL);
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
            hw_uart_driver_poll(&port->backend.hw);
        }
    }
}

void uart_driver_poll_pio(void)
{
    for (size_t index = UART_PORT_2; index < UART_PORT_COUNT; ++index) {
        uart_driver_port_t *port = &uart_ports[index];

        if (port->backend.pio.initialized) {
            pio_uart_driver_poll(&port->backend.pio);
        }
    }
}

size_t uart_driver_read(uart_port_id_t port_id, uint8_t *data, size_t capacity)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    ring_buffer_t *rx_ring;

    if ((port == NULL) || (data == NULL) || !uart_driver_port_is_ready(port_id)) {
        return 0u;
    }

    rx_ring = uart_driver_rx_ring_mutable(port);
    if (rx_ring == NULL) {
        return 0u;
    }

    return ring_buffer_read(rx_ring, data, capacity);
}

size_t uart_driver_write_available(uart_port_id_t port_id)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    ring_buffer_t *tx_ring;

    if ((port == NULL) || !uart_driver_port_is_ready(port_id) ||
        ((uart_driver_port_status_flags[port_id] & UART_DRIVER_PORT_STATUS_CONTROL_PENDING) != 0u)) {
        return 0u;
    }

    tx_ring = uart_driver_tx_ring_mutable(port);
    if (tx_ring == NULL) {
        return 0u;
    }

    return ring_buffer_free_space(tx_ring);
}

static uart_driver_command_status_t uart_driver_set_line_coding_local(
    uart_port_id_t port_id,
    const uart_driver_line_coding_t *line_coding,
    uint32_t *result_port_id)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    uart_driver_pending_control_t *pending_control;

    if (result_port_id != NULL) {
        *result_port_id = (uint32_t)port_id;
    }

    if (port == NULL) {
        return UART_DRIVER_COMMAND_STATUS_INVALID_PORT;
    }

    if (!uart_driver_line_coding_is_valid(line_coding)) {
        uart_driver_set_port_status_flag(port_id, UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
        return UART_DRIVER_COMMAND_STATUS_INVALID_ARGUMENT;
    }

    pending_control = &uart_driver_pending_controls[port_id];

    if (port->info.backend == UART_DRIVER_BACKEND_HW) {
        pending_control->line_coding = *line_coding;
    } else if (port->info.backend == UART_DRIVER_BACKEND_PIO) {
        if (!uart_driver_pio_line_coding_supported(line_coding)) {
            uart_driver_set_port_status_flag(port_id, UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
            return UART_DRIVER_COMMAND_STATUS_BACKEND_REJECTED;
        }

        pending_control->line_coding = *line_coding;
    } else {
        uart_driver_set_port_status_flag(port_id, UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
        return UART_DRIVER_COMMAND_STATUS_UNSUPPORTED;
    }

    pending_control->pending = true;
    uart_driver_set_port_status_flag(port_id, UART_DRIVER_PORT_STATUS_CONTROL_PENDING);
    uart_driver_clear_port_status_flag(port_id, UART_DRIVER_PORT_STATUS_CONTROL_ERROR);
    return UART_DRIVER_COMMAND_STATUS_QUEUED;
}

bool uart_driver_set_line_coding(uart_port_id_t port_id,
                                 const uart_driver_line_coding_t *line_coding)
{
    if (!uart_driver_worker_started) {
        uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_WORKER_NOT_STARTED, port_id);
        return false;
    }

    if (!uart_driver_line_coding_is_valid(line_coding)) {
        uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_INVALID_ARGUMENT, port_id);
        return false;
    }

    return uart_driver_mailbox_call(UART_DRIVER_MAILBOX_COMMAND_SET_LINE_CODING,
                                    (uint32_t)port_id,
                                    line_coding);
}

bool uart_driver_queue_line_coding(uart_port_id_t port_id,
                                   const uart_driver_line_coding_t *line_coding)
{
    uint32_t request_sequence;

    if (!uart_driver_worker_started) {
        uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_WORKER_NOT_STARTED, port_id);
        return false;
    }

    if (!uart_driver_line_coding_is_valid(line_coding)) {
        uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_INVALID_ARGUMENT, port_id);
        return false;
    }

    if (uart_driver_mailbox.request_sequence != uart_driver_mailbox.response_sequence) {
        return false;
    }

    request_sequence = uart_driver_mailbox.request_sequence + 1u;
    uart_driver_mailbox.port_id = (uint32_t)port_id;
    uart_driver_mailbox.line_coding = *line_coding;
    uart_driver_mailbox.command = UART_DRIVER_MAILBOX_COMMAND_SET_LINE_CODING;
    __dmb();
    uart_driver_mailbox.request_sequence = request_sequence;
    uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_QUEUED, port_id);
    return true;
}

bool uart_driver_set_baud_rate(uart_port_id_t port_id, uint32_t baud_rate)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    uart_driver_line_coding_t line_coding;

    if (!uart_driver_worker_started) {
        uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_WORKER_NOT_STARTED, port_id);
        return false;
    }

    if (!uart_driver_current_line_coding(port, &line_coding)) {
        uart_driver_set_last_status(UART_DRIVER_COMMAND_STATUS_INVALID_PORT, port_id);
        return false;
    }

    line_coding.baud_rate = baud_rate;
    return uart_driver_set_line_coding(port_id, &line_coding);
}

uint8_t uart_driver_port_status(uart_port_id_t port_id)
{
    if (port_id >= UART_PORT_COUNT) {
        return 0u;
    }

    return uart_driver_port_status_flags[port_id];
}

uart_driver_command_status_t uart_driver_last_command_status(void)
{
    return uart_driver_last_status;
}

uart_port_id_t uart_driver_last_command_port(void)
{
    return uart_driver_last_status_port;
}

bool uart_driver_worker_is_running(void)
{
    return uart_driver_worker_started;
}

uart_port_id_t uart_driver_worker_poll_port(void)
{
    return uart_driver_worker_poll_port_id;
}

uint8_t uart_driver_worker_heartbeat(void)
{
    return uart_driver_worker_heartbeat_value;
}

uint8_t uart_driver_hardfault_core(void)
{
    return uart_driver_hardfault_core_value;
}

size_t uart_driver_write(uart_port_id_t port_id, const uint8_t *data, size_t length)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);
    ring_buffer_t *tx_ring;

    if ((port == NULL) || (data == NULL) || !uart_driver_port_is_ready(port_id) ||
        ((uart_driver_port_status_flags[port_id] & UART_DRIVER_PORT_STATUS_CONTROL_PENDING) != 0u)) {
        return 0u;
    }

    tx_ring = uart_driver_tx_ring_mutable(port);
    if (tx_ring == NULL) {
        return 0u;
    }

    return ring_buffer_write(tx_ring, data, length);
}

const uart_driver_port_info_t *uart_driver_port_info(uart_port_id_t port_id)
{
    if (port_id >= UART_PORT_COUNT) {
        return NULL;
    }

    return &uart_ports[port_id].info;
}

bool uart_driver_port_pio_stats(uart_port_id_t port_id, uart_driver_pio_stats_t *stats)
{
    uart_driver_port_t *port = uart_driver_port_mutable(port_id);

    if ((port == NULL) || (stats == NULL) || (port->info.backend != UART_DRIVER_BACKEND_PIO)) {
        return false;
    }

    stats->rx_framing_error_count = (uint32_t)port->backend.pio.rx_framing_error_count;
    stats->tx_dma_claim_failure_count = (uint32_t)port->backend.pio.tx_dma_claim_failure_count;
    stats->tx_polled_bytes = (uint32_t)port->backend.pio.tx_polled_bytes;
    stats->tx_dma_bytes = (uint32_t)port->backend.pio.tx_dma_bytes;
    return true;
}

bool uart_driver_validate_topology(void)
{
    size_t hw_count = 0u;
    size_t pio_count = 0u;

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        if (uart_ports[index].info.id != (uart_port_id_t)index) {
            return false;
        }

        if (uart_ports[index].info.backend == UART_DRIVER_BACKEND_HW) {
            hw_count += 1u;
            continue;
        }

        if (uart_ports[index].info.backend == UART_DRIVER_BACKEND_PIO) {
            pio_count += 1u;
            continue;
        }

        return false;
    }

    return (hw_count == 2u) && (pio_count == 4u);
}