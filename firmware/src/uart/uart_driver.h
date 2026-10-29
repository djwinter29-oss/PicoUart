/**
 * @file uart_driver.h
 * @brief Logical UART port table for the PicoUart firmware.
 */

#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include "uart/types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Marker for an optional logical-port GPIO that is not assigned. */
#define UART_DRIVER_PIN_UNASSIGNED ((uint32_t)UINT32_MAX)

/** @brief Port status flag: backend is initialized and available. */
#define UART_DRIVER_PORT_STATUS_READY (1u << 0)
/** @brief Port status flag: backend init failed for this port. */
#define UART_DRIVER_PORT_STATUS_INIT_FAILED (1u << 1)
/** @brief Port status flag: the most recent control command for this port failed. */
#define UART_DRIVER_PORT_STATUS_CONTROL_ERROR (1u << 2)
/** @brief Port status flag: ingress is paused while the worker applies a control change at a safe backend boundary. */
#define UART_DRIVER_PORT_STATUS_CONTROL_PENDING (1u << 3)

/** @brief Retained reason for the last authoritative line-configuration failure. */
typedef enum {
    UART_CONTROL_ERROR_NONE = 0,    /**< No failure recorded since boot. */
    UART_CONTROL_ERROR_BAUD = 1,    /**< Unsupported baud request. */
    UART_CONTROL_ERROR_FRAMING = 2, /**< Unsupported data/parity/stop format. */
    UART_CONTROL_ERROR_BACKEND = 3, /**< Backend could not apply the configuration. */
    UART_CONTROL_ERROR_TIMEOUT = 4, /**< Mailbox or backend apply deadline expired. */
    UART_CONTROL_ERROR_OTHER = 5,   /**< Failure without a more specific reason. */
} uart_driver_control_error_t;

/** @brief Boot-lifetime configuration-error history protected by the driver status lock. */
typedef struct {
    uart_driver_control_error_t reason; /**< Last recorded failure, independent of current status. */
    uint64_t uptime_us; /**< Monotonic observation time of that failure. */
} uart_driver_control_history_t;

/**
 * @brief Backend-neutral startup configuration and runtime view of one logical UART port.
 *
 * The static configuration table supplies startup values. Runtime copies reflect the last
 * successfully applied host line coding while retaining the static pin map.
 */
typedef struct {
    uart_port_id_t id;             /**< Logical port identifier. */
    uart_driver_backend_t backend; /**< Backend class assigned to the port. */
    uint32_t baud_rate;            /**< Startup default; updated after a successful line-coding apply. */
    uint32_t tx_pin;               /**< Configured TX GPIO. */
    uint32_t rx_pin;               /**< Configured RX GPIO. */
    uint32_t rts_pin;              /**< Configured RTS GPIO, or @ref UART_DRIVER_PIN_UNASSIGNED. */
    uint32_t cts_pin;              /**< Configured CTS GPIO, or @ref UART_DRIVER_PIN_UNASSIGNED. */
    uint8_t data_bits;             /**< Initial value; updated after a successful line-coding apply. */
    uint8_t stop_bits;             /**< Initial value; updated after a successful line-coding apply. */
    uart_driver_parity_t parity;   /**< Initial value; updated after a successful line-coding apply. */
} uart_driver_port_info_t;

/** @brief Immutable startup records defined by the firmware UART configuration. */
extern const uart_driver_port_info_t uart_board_ports[UART_PORT_COUNT];

/**
 * @brief Transport counters for one logical UART port.
 */
typedef struct {
    uint32_t uart_tx_bytes;                   /**< Bytes handed to the UART peripheral/FIFO, not wire completion. */
    uint32_t uart_rx_bytes;                   /**< DMA-received bytes published into the RX ring. */
    uint16_t tx_ring_high_watermark;         /**< Largest observed USB-to-UART ring occupancy. */
    uint16_t rx_ring_high_watermark;         /**< Largest observed UART-to-USB ring occupancy. */
    uint32_t tx_ring_overflow_count;         /**< Consumer-recovery discards. Backpressure does not increment this. */
    uint32_t rx_ring_overflow_count;         /**< Bytes dropped by the UART-to-USB ring. */
    uint32_t rx_ring_pending_overflow_count; /**< Unread UART-to-USB bytes already overwritten. */
    uint32_t rx_error_count; /**< Controller receive-status events observed since the post-boot baseline. */
} uart_driver_port_stats_t;

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
 * @brief Queue a per-port backend stop from the USB core to the UART worker.
 * @param port_id Logical UART port identifier.
 * @return `true` only for a newly queued stop; `false` for an invalid port or an already-pending stop.
 */
bool uart_driver_request_channel_stop(uart_port_id_t port_id);

/**
 * @brief Queue a per-port backend start after a CDC DTR-open transition.
 * @param port_id Logical UART port identifier.
 * @return `true` when the start request is queued or already pending.
 */
bool uart_driver_request_channel_start(uart_port_id_t port_id);

/**
 * @brief Return whether a channel start is waiting for the UART worker.
 * @param port_id Logical UART port identifier.
 * @return `true` while a start request is pending.
 */
bool uart_driver_channel_start_pending(uart_port_id_t port_id);

/**
 * @brief Return whether a per-port stop is holding the CDC bridge.
 * @param port_id Logical UART port identifier.
 * @return `true` while the stop request is pending.
 */
bool uart_driver_channel_stop_pending(uart_port_id_t port_id);

/**
 * @brief Mark a port quiesced after the USB core stops bridging and clears CDC FIFOs.
 * @param port_id Logical UART port identifier.
 */
void uart_driver_channel_stop_usb_quiesced(uart_port_id_t port_id);

/**
 * @brief Return whether a queued port reset is ready for the worker.
 * @param port_id Logical UART port identifier.
 * @return `true` when pending and USB-side access is quiesced.
 */
bool uart_driver_channel_stop_is_quiesced(uart_port_id_t port_id);

/**
 * @brief Drain RX bytes from one logical UART port into a caller-owned writer.
 * @param port_id Logical port identifier.
 * @param capacity Maximum byte count to drain across contiguous RX spans.
 * @param writer Caller callback that writes some or all of the offered bytes.
 * @param context Opaque caller context passed to @p writer.
 * @param snapshot Caller-owned storage for a stable RX copy.
 * @param snapshot_capacity Size of @p snapshot in bytes.
 * @return Number of bytes committed from the RX ring.
 */
size_t uart_driver_drain_rx(uart_port_id_t port_id, size_t capacity,
                            uint32_t (*writer)(void *context, const uint8_t *data, uint32_t length), void *context,
                            uint8_t *snapshot, size_t snapshot_capacity);

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
size_t uart_driver_fill_tx(uart_port_id_t port_id, size_t capacity,
                           uint32_t (*reader)(void *context, uint8_t *data, uint32_t length), void *context);

/**
 * @brief Queue one host line-coding request without waiting for the UART worker.
 * @param port_id Logical port identifier.
 * @param line_coding Requested baud/data/parity/stop configuration.
 * @return `true` when the worker mailbox accepted the request and ingress is
 * paused until the worker applies or rejects it, otherwise `false`.
 */
bool uart_driver_queue_line_coding(uart_port_id_t port_id, const uart_driver_line_coding_t *line_coding,
                                   uint32_t control_generation);

/**
 * @brief Return whether TX ingress is paused for an active line-coding transition.
 * @param port_id Logical UART port identifier.
 * @return `true` while any control-pending owner holds the port.
 */
bool uart_driver_port_tx_is_blocked(uart_port_id_t port_id);

/**
 * @brief Return whether @p line_coding can ever be applied to @p port_id.
 * @param port_id Logical port identifier.
 * @param line_coding Host-requested baud/data/parity/stop configuration.
 * @return `false` for invalid ports/args or permanent backend rejects (for
 * example PIO non-8N1 or infeasible baud). Does not consider mailbox busyness.
 */
bool uart_driver_line_coding_acceptable(uart_port_id_t port_id, const uart_driver_line_coding_t *line_coding);

/**
 * @brief Mark one logical UART port as having failed its latest control request.
 * @param port_id Logical port identifier.
 * @param reason Retained authoritative configuration-error reason.
 *
 * Used when USB CDC accepts a `SET_LINE_CODING` transfer that the firmware
 * cannot parse or apply, so hosts can observe @ref UART_DRIVER_PORT_STATUS_CONTROL_ERROR
 * through HID even though the USB control transfer itself succeeded.
 *
 * Advances the port control generation so a later completion from an older request
 * cannot clear this error. Does **not** clear @ref UART_DRIVER_PORT_STATUS_CONTROL_PENDING.
 */
void uart_driver_report_control_error(uart_port_id_t port_id, uart_driver_control_error_t reason);

/**
 * @brief Report a CDC soft-pending failure without racing worker-owned control state.
 * @param port_id Logical port identifier.
 * @param control_generation Generation of the soft-pending request that failed.
 * @param reason Retained authoritative configuration-error reason.
 *
 * Sets @ref UART_DRIVER_PORT_STATUS_CONTROL_ERROR and clears
 * @ref UART_DRIVER_PORT_STATUS_CONTROL_PENDING only when the worker has not
 * accepted the request for deferred application.
 */
void uart_driver_report_soft_pending_error(uart_port_id_t port_id, uint32_t control_generation,
                                           uart_driver_control_error_t reason);

/**
 * @brief Capture retained configuration-error history without clearing it.
 * @param port_id Logical port identifier.
 * @param history Caller-owned output, unchanged on failure.
 * @return True when captured under the status lock.
 */
bool uart_driver_control_history(uart_port_id_t port_id, uart_driver_control_history_t *history);

/**
 * @brief Drop USB-core soft-pending ownership after host disconnect.
 *
 * Worker/mailbox-owned requests remain untouched and finish on core 1.
 */
void uart_driver_reset_soft_pending(uart_port_id_t port_id);

/**
 * @brief Mark one logical UART port as having a control request in flight.
 * @param port_id Logical port identifier.
 *
 * Used by the CDC soft-pending path so HID `control_pending` is visible while a
 * line-coding request waits for the worker mailbox (before
 * @ref uart_driver_queue_line_coding accepts it).
 * @return Generation identifying the newly pending request, or zero for an invalid port.
 */
uint32_t uart_driver_mark_control_pending(uart_port_id_t port_id);

/**
 * @brief Return the status flags for one logical UART port.
 * @param port_id Logical port identifier.
 * @return Bitwise OR of @ref UART_DRIVER_PORT_STATUS_* flags.
 */
uint8_t uart_driver_port_status(uart_port_id_t port_id);

/**
 * @brief Capture applied settings and control health under their shared ownership lock.
 * @param port_id Logical UART port identifier.
 * @param info Output storage for the applied settings and board pins.
 * @param status Output storage for driver status flags.
 * @return `true` when both values were captured coherently.
 */
bool uart_driver_port_snapshot(uart_port_id_t port_id, uart_driver_port_info_t *info, uint8_t *status);

/**
 * @brief Return whether the dedicated UART worker core is running.
 * @return `true` after the worker core has been launched.
 */
bool uart_driver_worker_is_running(void);

/**
 * @brief Return whether the UART worker core is still publishing heartbeats.
 * @return `true` when the worker counter advanced within the stale window.
 */
bool uart_driver_worker_heartbeat_is_fresh(void);

/**
 * @brief Return public metadata for one logical UART port.
 * @param port_id Logical port identifier.
 * @param info Output storage for a coherent metadata snapshot.
 * @return `true` when @p info was written, otherwise `false`.
 * @note `baud_rate` reflects the last successfully applied line coding.
 */
bool uart_driver_port_info(uart_port_id_t port_id, uart_driver_port_info_t *info);

/**
 * @brief Snapshot transport counters for one logical UART port.
 * @param port_id Logical port identifier.
 * @param stats Output storage, unchanged on every failed acquisition.
 * @return `true` when @p stats was written, otherwise `false`.
 * @note Initialized ring storage remains readable after backend stop; readiness is independent of validity.
 */
bool uart_driver_port_stats(uart_port_id_t port_id, uart_driver_port_stats_t *stats);

#endif
