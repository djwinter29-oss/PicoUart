/**
 * @file worker.h
 * @brief Core-1 UART worker scheduling and heartbeat health.
 */

#ifndef UART_WORKER_H
#define UART_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Stale window after which a silent UART worker is treated as hung (ms).
 *
 * Longer than a 1 s deferred line-coding wait and TXSTALL re-assert at BAUD_MIN,
 * shorter than the 8 s USB watchdog so core 0 can stop petting and still reset.
 */
#define UART_WORKER_HEARTBEAT_STALE_MS 2000u

/** @brief One driver-owned operation invoked by the UART worker. */
typedef void (*uart_worker_hook_t)(void);

/**
 * @brief Driver hooks invoked by the UART worker in one deterministic order.
 */
typedef struct {
    uart_worker_hook_t initialize;      /**< Enable worker-core backend service. */
    uart_worker_hook_t service_control; /**< Apply mailbox and deferred control work. */
    uart_worker_hook_t poll_io;         /**< Advance all backend I/O. */
    volatile uint32_t *heartbeat;       /**< Counter published after every completed sweep. */
} uart_worker_hooks_t;

/**
 * @brief Run one UART worker sweep.
 * @param hooks Driver operations and heartbeat storage.
 */
void uart_worker_step(const uart_worker_hooks_t *hooks);

/**
 * @brief Initialize and run the UART worker loop forever on core 1.
 * @param hooks Driver operations and heartbeat storage.
 */
void uart_worker_run(const uart_worker_hooks_t *hooks);

/**
 * @brief Track whether a worker heartbeat counter is still advancing.
 * @param heartbeat Latest counter published by the UART worker core.
 * @param last_heartbeat In/out previous observed counter.
 * @param now_ms Current `to_ms_since_boot` sample.
 * @param last_change_ms In/out time when @p heartbeat last differed from @p last_heartbeat.
 * @param stale_ms Hang threshold.
 * @return `true` when the worker has incremented within @p stale_ms.
 */
static inline bool uart_worker_heartbeat_is_fresh(uint32_t heartbeat, uint32_t *last_heartbeat, uint32_t now_ms,
                                                  uint32_t *last_change_ms, uint32_t stale_ms)
{
    if ((last_heartbeat == NULL) || (last_change_ms == NULL) || (stale_ms == 0u)) {
        return false;
    }

    if (heartbeat != *last_heartbeat) {
        *last_heartbeat = heartbeat;
        *last_change_ms = now_ms;
        return true;
    }

    return (int32_t)(now_ms - *last_change_ms) < (int32_t)stale_ms;
}

#endif