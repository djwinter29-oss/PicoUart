/**
 * @file uart.h
 * @brief Host-test stub for Pico SDK hardware/uart.h.
 */

#ifndef HARDWARE_UART_H
#define HARDWARE_UART_H

typedef struct uart_inst uart_inst_t;

typedef enum {
    UART_PARITY_NONE,
    UART_PARITY_EVEN,
    UART_PARITY_ODD,
} uart_parity_t;

#endif