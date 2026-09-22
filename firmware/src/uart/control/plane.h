/**
 * @file plane.h
 * @brief Private deferred UART line-coding control plane.
 */

#ifndef UART_CONTROL_PLANE_H
#define UART_CONTROL_PLANE_H

#include "uart/control/mailbox.h"
#include "uart/runtime.h"

#include "hardware/sync.h"
#include "pico/time.h"

/**
 * @brief Deferred worker-owned line-coding state for one logical UART port.
 */
typedef struct {
    bool pending; /**< True while a line-coding change is waiting to be applied. */
    uint32_t control_generation; /**< Host request generation that owns @ref pending. */
    uint32_t tx_boundary_sequence; /**< Last TX byte admitted under the old line format. */
    absolute_time_t deadline; /**< Absolute time when a deferred apply must succeed or fail. */
    uart_driver_line_coding_t line_coding; /**< Latest requested line-coding payload. */
} uart_control_pending_t;

/**
 * @brief Private state owned by the UART control plane.
 */
typedef struct {
    uart_runtime_port_t *ports; /**< Logical UART runtime ports. */
    uart_control_mailbox_t *mailboxes; /**< One core-0 to worker request slot per port. */
    uart_control_pending_t *pending_controls; /**< Worker-owned deferred requests. */
    bool *soft_pending_controls; /**< Core-0 pending-request flags. */
    uint32_t *control_generations; /**< Latest host generation per port. */
    volatile uint8_t *status_flags; /**< Shared per-port status flags. */
    spin_lock_t *status_lock; /**< Lock protecting shared control status. */
    volatile uint32_t *stats_sequence; /**< Per-port telemetry snapshot sequences. */
    size_t *poll_start_index; /**< Next port to service first. */
} uart_control_plane_t;

/**
 * @brief Consume each port's mailbox request and service deferred line-coding changes.
 * @param control_plane Private runtime state.
 */
void uart_control_plane_service(uart_control_plane_t *control_plane);

/**
 * @brief Return whether a port may launch new TX bytes during worker polling.
 * @param control_plane Private runtime state.
 * @param port_id Logical UART port.
 * @return `true` when launching a new TX transfer is safe.
 */
bool uart_control_plane_tx_launch_allowed(const uart_control_plane_t *control_plane,
                                          uart_port_id_t port_id);

#endif