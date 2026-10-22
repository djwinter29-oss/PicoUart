/**
 * @file uart.h
 * @brief Host-test stub for Pico SDK hardware/uart.h.
 */

#ifndef HARDWARE_UART_H
#define HARDWARE_UART_H

typedef struct uart_inst {
    unsigned int test_identity;
} uart_inst_t;

extern uart_inst_t uart0_test_instance;
extern uart_inst_t uart1_test_instance;

#define uart0 (&uart0_test_instance)
#define uart1 (&uart1_test_instance)

typedef enum {
    UART_PARITY_NONE,
    UART_PARITY_EVEN,
    UART_PARITY_ODD,
} uart_parity_t;

#endif