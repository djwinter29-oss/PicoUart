/**
 * @file capacity_config.h
 * @brief Shared fixed-capacity configuration for PicoUart firmware.
 *
 * These values define statically allocated USB and UART storage as well as
 * bounded per-poll transfer work. Changes affect memory usage, DMA ring
 * geometry, or transport latency and must preserve the assertions below.
 */

#ifndef PICO_UART_CAPACITY_CONFIG_H
#define PICO_UART_CAPACITY_CONFIG_H

/** @brief USB control endpoint transfer capacity in bytes. */
#define PICO_UART_USB_CONTROL_ENDPOINT_BUFFER_SIZE 64u
/** @brief USB CDC notification endpoint transfer capacity in bytes. */
#define PICO_UART_USB_CDC_NOTIFICATION_ENDPOINT_BUFFER_SIZE 8u
/** @brief USB CDC receive FIFO capacity in bytes per CDC interface. */
#define PICO_UART_USB_CDC_RX_BUFFER_SIZE 2048u
/** @brief USB CDC transmit FIFO capacity in bytes per CDC interface. */
#define PICO_UART_USB_CDC_TX_BUFFER_SIZE 2048u
/** @brief USB CDC endpoint transfer capacity in bytes per CDC interface. */
#define PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE 64u
/** @brief Maximum bytes moved per CDC interface and direction in one bridge pass. */
#define PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET 1024u
/** @brief USB HID endpoint transfer capacity in bytes. */
#define PICO_UART_USB_HID_ENDPOINT_BUFFER_SIZE 64u

/** @brief Hardware UART receive ring capacity in bytes. Must be a power of two. */
#define PICO_UART_HW_UART_RX_BUFFER_SIZE 4096u
/** @brief Hardware UART transmit ring capacity in bytes. Must be a power of two. */
#define PICO_UART_HW_UART_TX_BUFFER_SIZE 4096u
/** @brief Hardware UART RX DMA ring selector bits for the receive-ring capacity. */
#define PICO_UART_HW_UART_RX_DMA_RING_BITS 12u

_Static_assert(PICO_UART_HW_UART_RX_BUFFER_SIZE ==
				   (1u << PICO_UART_HW_UART_RX_DMA_RING_BITS),
			   "Hardware UART RX DMA ring bits must match its buffer size");

/** @brief PIO UART receive ring capacity in bytes. Must be a power of two. */
#define PICO_UART_PIO_UART_RX_BUFFER_SIZE 4096u
/** @brief PIO UART transmit ring capacity in bytes. Must be a power of two. */
#define PICO_UART_PIO_UART_TX_BUFFER_SIZE 4096u
/** @brief PIO UART RX DMA ring selector bits for the receive-ring capacity. */
#define PICO_UART_PIO_UART_RX_DMA_RING_BITS 12u
/** @brief PIO UART TX backlog in bytes that starts DMA transmission. */
#define PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD 64u

_Static_assert(PICO_UART_PIO_UART_RX_BUFFER_SIZE ==
                                   (1u << PICO_UART_PIO_UART_RX_DMA_RING_BITS),
                           "PIO UART RX DMA ring bits must match its buffer size");

#endif