/**
 * @file hw_uart_sdk.h
 * @brief Isolated UART declarations extending the shared driver-test SDK surface.
 */
#ifndef HW_UART_TEST_SDK_H
#define HW_UART_TEST_SDK_H

#include "pio_tx_sdk.h"

/** @brief PL011 registers accessed by the real HW UART implementation. */
typedef struct {
    volatile uint32_t dr; /**< Data register and DMA endpoint. */
    volatile uint32_t rsr; /**< Sticky receive errors. */
    volatile uint32_t fr; /**< UART flags, including BUSY. */
} uart_hw_t;

/** @brief Mock UART instance retaining independent register state. */
typedef struct uart_inst {
    uart_hw_t registers; /**< UART register storage. */
} uart_inst_t;

typedef enum {
    UART_PARITY_NONE, /**< SDK no-parity encoding. */
    UART_PARITY_EVEN, /**< SDK even-parity encoding. */
    UART_PARITY_ODD, /**< SDK odd-parity encoding. */
} uart_parity_t;

extern uart_inst_t hw_test_uart0;
extern uart_inst_t hw_test_uart1;
#define uart0 (&hw_test_uart0)
#define uart1 (&hw_test_uart1)
#define DMA_IRQ_0 0u
#define GPIO_FUNC_UART 2u
#define GPIO_FUNC_NULL 31u
#define UART_UARTFR_BUSY_BITS 8u
#define UART_UARTRSR_BITS 15u
#define clk_peri 1u

uint32_t uart_init(uart_inst_t *uart, uint32_t baud_rate);
void uart_deinit(uart_inst_t *uart);
uart_hw_t *uart_get_hw(uart_inst_t *uart);
uint uart_get_dreq(uart_inst_t *uart, bool tx);
bool uart_is_readable(uart_inst_t *uart);
void uart_set_hw_flow(uart_inst_t *uart, bool cts, bool rts);
void uart_set_format(uart_inst_t *uart, uint data_bits, uint stop_bits, uart_parity_t parity);
void uart_set_fifo_enabled(uart_inst_t *uart, bool enabled);
void gpio_disable_pulls(uint pin);

#endif