/**
 * @file uart_driver.h
 * @brief Logical UART port table for the PicoUart firmware.
 */

#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Backend class used by a logical UART port.
 */
typedef enum {
    UART_DRIVER_BACKEND_HW = 0, /**< Hardware UART peripheral backend. */
    UART_DRIVER_BACKEND_PIO, /**< PIO UART backend. */
} uart_driver_backend_t;

/**
 * @brief Result code returned by cross-core UART control commands.
 */
typedef enum {
    UART_DRIVER_COMMAND_STATUS_OK = 0, /**< Command completed successfully. */
    UART_DRIVER_COMMAND_STATUS_WORKER_NOT_STARTED = 1, /**< UART worker core has not started yet. */
    UART_DRIVER_COMMAND_STATUS_INVALID_PORT = 2, /**< Command referenced an invalid logical port. */
    UART_DRIVER_COMMAND_STATUS_INVALID_ARGUMENT = 3, /**< Command arguments were malformed or unsupported. */
    UART_DRIVER_COMMAND_STATUS_INIT_FAILED = 4, /**< One or more UART backends failed during init. */
    UART_DRIVER_COMMAND_STATUS_BACKEND_REJECTED = 5, /**< Backend rejected the requested control change. */
    UART_DRIVER_COMMAND_STATUS_UNSUPPORTED = 6, /**< Command could not be applied to the selected backend. */
    UART_DRIVER_COMMAND_STATUS_QUEUED = 7, /**< Command was accepted and queued for deferred worker-side apply. */
    UART_DRIVER_COMMAND_STATUS_TIMEOUT = 8, /**< Worker did not complete the command before its deadline. */
} uart_driver_command_status_t;

/** @brief Port status flag: backend is initialized and available. */
#define UART_DRIVER_PORT_STATUS_READY (1u << 0)
/** @brief Port status flag: backend init failed for this port. */
#define UART_DRIVER_PORT_STATUS_INIT_FAILED (1u << 1)
/** @brief Port status flag: the most recent control command for this port failed. */
#define UART_DRIVER_PORT_STATUS_CONTROL_ERROR (1u << 2)
/** @brief Port status flag: ingress is paused while the worker applies a control change at a safe backend boundary. */
#define UART_DRIVER_PORT_STATUS_CONTROL_PENDING (1u << 3)

/**
 * @brief Logical UART port identifiers.
 */
typedef enum {
    UART_PORT_0 = 0, /**< Logical UART port 0. */
    UART_PORT_1, /**< Logical UART port 1. */
    UART_PORT_2, /**< Logical UART port 2. */
    UART_PORT_3, /**< Logical UART port 3. */
    UART_PORT_4, /**< Logical UART port 4. */
    UART_PORT_5, /**< Logical UART port 5. */
    UART_PORT_COUNT /**< Total number of logical UART ports. */
} uart_port_id_t;

/**
 * @brief Public view of one logical UART port.
 */
typedef struct {
    uart_port_id_t id; /**< Logical port identifier. */
    uart_driver_backend_t backend; /**< Backend class assigned to the port. */
    uint32_t baud_rate; /**< Configured startup baud rate. */
    uint32_t tx_pin; /**< Configured TX GPIO, or unassigned marker. */
    uint32_t rx_pin; /**< Configured RX GPIO, or unassigned marker. */
} uart_driver_port_info_t;

/**
 * @brief Transport counters for one logical UART port.
 */
typedef struct {
    uint32_t controller_tx_bytes; /**< Bytes transmitted by the UART controller. */
    uint32_t controller_rx_bytes; /**< Bytes received by the UART controller. */
    uint16_t tx_ring_high_watermark; /**< Largest observed USB-to-UART ring occupancy. */
    uint16_t rx_ring_high_watermark; /**< Largest observed UART-to-USB ring occupancy. */
    uint32_t tx_ring_overflow_count; /**< Bytes rejected by the USB-to-UART ring. */
    uint32_t rx_ring_overflow_count; /**< Bytes dropped by the UART-to-USB ring. */
    uint32_t rx_ring_pending_overflow_count; /**< Unread UART-to-USB bytes already overwritten. */
    uint32_t rx_error_count; /**< Hardware UART receive-status events observed since initialization. */
} uart_driver_port_stats_t;

/**
 * @brief Host-requested UART parity mode.
 */
typedef enum {
    UART_DRIVER_PARITY_NONE = 0, /**< No parity bit. */
    UART_DRIVER_PARITY_ODD, /**< Odd parity. */
    UART_DRIVER_PARITY_EVEN, /**< Even parity. */
} uart_driver_parity_t;

/**
 * @brief Worker-applied UART line-coding request.
 */
typedef struct {
    uint32_t baud_rate; /**< Requested baud rate. */
    uint8_t data_bits; /**< Requested UART data-bit count. */
    uint8_t stop_bits; /**< Requested UART stop-bit count. */
    uart_driver_parity_t parity; /**< Requested parity mode. */
} uart_driver_line_coding_t;

/**
 * @brief Return the number of logical UART ports in the firmware.
 * @return Always 6 for the current design.
 */
size_t uart_driver_port_count(void);

/**
 * @brief Initialize all configured UART backend instances.
 * @return `true` when every configured backend initialized successfully, otherwise `false`.
 */
bool uart_driver_init(void);

/**
 * @brief Return whether one logical UART port is initialized and available.
 * @param port_id Logical port identifier.
 * @return `true` when the port is active, otherwise `false`.
 */
bool uart_driver_port_is_ready(uart_port_id_t port_id);

/**
 * @brief Advance DMA state for the two hardware UART backends.
 */
void uart_driver_poll_hardware(void);

/**
 * @brief Advance RX and TX state for the four PIO UART backends.
 */
void uart_driver_poll_pio(void);

/**
 * @brief Drain RX bytes from one logical UART port into a caller-owned writer.
 * @param port_id Logical port identifier.
 * @param capacity Maximum byte count to drain across contiguous RX spans.
 * @param writer Caller callback that writes some or all of the offered bytes.
 * @param context Opaque caller context passed to @p writer.
 * @return Number of bytes committed from the RX ring.
 */
size_t uart_driver_drain_rx(uart_port_id_t port_id,
                            size_t capacity,
                            uint32_t (*writer)(void *context, const uint8_t *data, uint32_t length),
                            void *context);

/**
 * @brief Advance RX overrun recovery for one logical UART port without draining.
 * @param port_id Logical port identifier.
 * @return Number of overwritten bytes retired from the RX ring, or zero.
 *
 * Call this from the USB poll even when the matching CDC IN endpoint has no
 * space, so a stalled host cannot leave the ring sequence ambiguous.
 */
size_t uart_driver_recover_rx(uart_port_id_t port_id);

/**
 * @brief Fill TX bytes for one logical UART port from a caller-owned reader.
 * @param port_id Logical port identifier.
 * @param capacity Maximum byte count to fill across contiguous TX spans.
 * @param reader Caller callback that provides bytes for the writable span.
 * @param context Opaque caller context passed to @p reader.
 * @return Number of bytes committed into the TX ring.
 */
size_t uart_driver_fill_tx(uart_port_id_t port_id,
                           size_t capacity,
                           uint32_t (*reader)(void *context, uint8_t *data, uint32_t length),
                           void *context);

/**
 * @brief Queue one host line-coding request without waiting for the UART worker.
 * @param port_id Logical port identifier.
 * @param line_coding Requested baud/data/parity/stop configuration.
 * @return `true` when the worker mailbox accepted the request and ingress is
 * paused until the worker applies or rejects it, otherwise `false`.
 */
bool uart_driver_queue_line_coding(uart_port_id_t port_id,
                                   const uart_driver_line_coding_t *line_coding);

/**
 * @brief Return the status flags for one logical UART port.
 * @param port_id Logical port identifier.
 * @return Bitwise OR of @ref UART_DRIVER_PORT_STATUS_* flags.
 */
uint8_t uart_driver_port_status(uart_port_id_t port_id);

/**
 * @brief Return whether the dedicated UART worker core is running.
 * @return `true` after the worker core has been launched.
 */
bool uart_driver_worker_is_running(void);

/**
 * @brief Return public metadata for one logical UART port.
 * @param port_id Logical port identifier.
 * @return Pointer to immutable port metadata, or `NULL` when the index is invalid.
 */
const uart_driver_port_info_t *uart_driver_port_info(uart_port_id_t port_id);

/**
 * @brief Snapshot transport counters for one logical UART port.
 * @param port_id Logical port identifier.
 * @param stats Output storage for the counter snapshot.
 * @return `true` when @p stats was written, otherwise `false`.
 */
bool uart_driver_port_stats(uart_port_id_t port_id, uart_driver_port_stats_t *stats);

/**
 * @brief Check that the logical UART table still matches the 2-HW and 4-PIO design.
 * @return `true` when the table is valid, otherwise `false`.
 */
bool uart_driver_validate_topology(void);

#endif