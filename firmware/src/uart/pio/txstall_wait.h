/**
 * @file txstall_wait.h
 * @brief Pure TXSTALL re-assert wait helper (host-testable).
 *
 * After write-clearing sticky TXSTALL, the SM must re-assert the flag within a
 * few PIO cycles when truly stalled on `pull`. One PIO cycle is
 * 1/(UART_LINE_CODING_PIO_CLOCKS_PER_BIT·baud) seconds.
 */

#ifndef PIO_UART_TXSTALL_WAIT_H
#define PIO_UART_TXSTALL_WAIT_H

#include "uart/line_coding.h"

#include <stdint.h>

/** @brief Minimum APB/high-baud floor for the TXSTALL re-assert poll (microseconds). */
#define PIO_UART_TXSTALL_REASSERT_WAIT_FLOOR_US 2u

/** @brief Number of PIO cycles to wait for TXSTALL to re-assert after W1C. */
#define PIO_UART_TXSTALL_REASSERT_PIO_CYCLES 3u

/** @brief ceil(PIO_CYCLES * 1e6 / clocks_per_bit), exact when the division divides. */
#define PIO_UART_TXSTALL_REASSERT_DIVIDEND \
    ((PIO_UART_TXSTALL_REASSERT_PIO_CYCLES * 1000000u) / UART_LINE_CODING_PIO_CLOCKS_PER_BIT)

_Static_assert((PIO_UART_TXSTALL_REASSERT_PIO_CYCLES * 1000000u) %
                       UART_LINE_CODING_PIO_CLOCKS_PER_BIT ==
                   0u,
               "TXSTALL wait dividend must divide evenly by clocks per bit");

/**
 * @brief Microseconds to wait for TXSTALL re-assert after write-clear.
 * @param baud Current PIO UART baud rate (0 treated as 1).
 * @return ceil((PIO_CYCLES * 1e6) / (clocks_per_bit * baud)), floored at
 *         @ref PIO_UART_TXSTALL_REASSERT_WAIT_FLOOR_US.
 */
static inline uint32_t pio_uart_txstall_reassert_wait_us(uint32_t baud)
{
    uint32_t wait_us;

    if (baud == 0u) {
        baud = 1u;
    }

    /* ceil(3e6 / (16 * baud)) == ceil(187500 / baud) at 16 clocks/bit. */
    wait_us = (PIO_UART_TXSTALL_REASSERT_DIVIDEND + baud - 1u) / baud;
    if (wait_us < PIO_UART_TXSTALL_REASSERT_WAIT_FLOOR_US) {
        wait_us = PIO_UART_TXSTALL_REASSERT_WAIT_FLOOR_US;
    }

    return wait_us;
}

#endif
