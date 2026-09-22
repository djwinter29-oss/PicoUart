/**
 * @file worker.c
 * @brief Core-1 UART worker scheduling loop.
 */

#include "uart/worker.h"

#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stddef.h>

void uart_worker_step(const uart_worker_hooks_t *hooks)
{
    if ((hooks == NULL) || (hooks->service_control == NULL) ||
        (hooks->poll_io == NULL) || (hooks->heartbeat == NULL)) {
        return;
    }

    hooks->service_control();
    hooks->poll_io();
    *hooks->heartbeat += 1u;
    __dmb();
}

void uart_worker_run(const uart_worker_hooks_t *hooks)
{
    if ((hooks == NULL) || (hooks->initialize == NULL)) {
        while (true) {
            tight_loop_contents();
        }
    }

    hooks->initialize();
    while (true) {
        uart_worker_step(hooks);
        tight_loop_contents();
    }
}