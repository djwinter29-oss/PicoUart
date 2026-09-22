/**
 * @file worker.h
 * @brief Core-1 UART worker scheduling loop.
 */

#ifndef UART_WORKER_H
#define UART_WORKER_H

#include <stdint.h>

/** @brief One driver-owned operation invoked by the UART worker. */
typedef void (*uart_worker_hook_t)(void);

/**
 * @brief Driver hooks invoked by the UART worker in one deterministic order.
 */
typedef struct {
    uart_worker_hook_t initialize; /**< Enable worker-core backend service. */
    uart_worker_hook_t service_control; /**< Apply mailbox and deferred control work. */
    uart_worker_hook_t poll_io; /**< Advance all backend I/O. */
    volatile uint32_t *heartbeat; /**< Counter published after every completed sweep. */
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

#endif