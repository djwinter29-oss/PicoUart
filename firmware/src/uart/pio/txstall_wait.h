/**
 * @file txstall_wait.h
 * @brief Nonblocking TXSTALL re-assertion policy shared by the driver and host tests.
 */

#ifndef PIO_UART_TXSTALL_WAIT_H
#define PIO_UART_TXSTALL_WAIT_H

#include <stdbool.h>

/** @brief Action for one worker-sweep sample of the sticky TXSTALL flag. */
typedef enum {
    PIO_UART_TXSTALL_WAIT, /**< No fresh idle evidence; service other ports before retrying. */
    PIO_UART_TXSTALL_CLEAR, /**< Clear a previously observed sticky flag to arm a fresh probe. */
    PIO_UART_TXSTALL_IDLE, /**< The flag reasserted after clear, with no intervening TX launch. */
} pio_uart_txstall_action_t;

/**
 * @brief Advance a fresh-idle probe without waiting for the PIO clock.
 * @param waiting Persistent worker-owned probe state; reset on TX launch and lifecycle changes.
 * @param stalled Current TXSTALL flag sampled while DMA is inactive and the TX FIFO is empty.
 * @return Whether to clear the stale flag, wait for another sweep, or accept fresh idle evidence.
 */
static inline pio_uart_txstall_action_t pio_uart_txstall_sample(bool *waiting, bool stalled)
{
    if (!*waiting) {
        if (!stalled) {
            return PIO_UART_TXSTALL_WAIT;
        }
        *waiting = true;
        return PIO_UART_TXSTALL_CLEAR;
    }

    if (!stalled) {
        return PIO_UART_TXSTALL_WAIT;
    }
    *waiting = false;
    return PIO_UART_TXSTALL_IDLE;
}

#endif
