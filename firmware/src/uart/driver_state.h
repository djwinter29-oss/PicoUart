/**
 * @file driver_state.h
 * @brief Private aggregate state for the top-level UART driver.
 */

#ifndef UART_DRIVER_STATE_H
#define UART_DRIVER_STATE_H

#include "hardware/sync.h"
#include "uart/control/plane.h"
#include "uart/port_api.h"

/**
 * @brief All mutable state owned by the top-level UART facade.
 *
 * Keeping the arrays together makes additions to the control and telemetry
 * paths explicit instead of silently creating another parallel global array.
 */
typedef struct {
    uart_runtime_port_t ports[UART_PORT_COUNT]; /**< Backend storage and public metadata. */
    uart_control_mailbox_t mailbox; /**< Core-0 to core-1 control request slot. */
    bool worker_started; /**< True after core 1 has been launched. */
    volatile uint32_t worker_heartbeat; /**< Core-1 worker progress counter. */
    volatile uint8_t status_flags[UART_PORT_COUNT]; /**< Per-port health flags. */
    spin_lock_t *status_lock; /**< Lock protecting shared status and generations. */
    uart_control_pending_t pending_controls[UART_PORT_COUNT]; /**< Worker-owned control state. */
    bool soft_pending_controls[UART_PORT_COUNT]; /**< Core-0 mailbox wait state. */
    uint32_t control_generations[UART_PORT_COUNT]; /**< Latest host generation per port. */
    volatile uint32_t stats_sequence[UART_PORT_COUNT]; /**< Coherent stats snapshot sequence. */
    size_t poll_start_index; /**< First port in the next worker control sweep. */
    uart_control_plane_t control_plane; /**< Worker-side deferred control context. */
    uart_port_api_t port_api; /**< Public port facade context. */
} uart_driver_state_t;

#endif