/**
 * @file pio_uart_driver.h
 * @brief PIO UART backend for PicoUart logical UART ports.
 */

#ifndef PIO_UART_DRIVER_H
#define PIO_UART_DRIVER_H

#include "config/capacity_config.h"
#include "hardware/pio.h"
#include "uart/uart_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief No optional board-level GPIO policy flags are enabled. */
#define PIO_UART_DRIVER_PIN_FLAG_NONE 0u
/** @brief Enable a pull-up on the configured RX pin during backend init. */
#define PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP (1u << 0)
/** @brief Require a high (idle) RX line as an extra guard before applying a deferred baud change. */
#define PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH (1u << 1)
/** @brief Drive the configured RTS pin from RX-ring occupancy. */
#define PIO_UART_DRIVER_PIN_FLAG_RX_FLOW_CONTROL (1u << 2)
/** @brief Gate TX frame starts on the configured active-low CTS pin. */
#define PIO_UART_DRIVER_PIN_FLAG_TX_FLOW_CONTROL (1u << 3)
/**
 * @brief Static configuration for one PIO UART instance.
 */
typedef struct {
    const uart_driver_port_info_t *port; /**< Immutable board wiring and startup line coding. */
    PIO pio;                         /**< PIO block assigned to the logical UART. */
    uint32_t tx_state_machine;       /**< PIO TX state machine index. */
    uint32_t rx_state_machine;       /**< PIO RX state machine index. */
    uint32_t pin_flags;              /**< Explicit board-level GPIO policy flags. */
    uint32_t tx_dma_start_threshold; /**< TX backlog threshold that triggers a DMA transfer, or 0 for the default. */
} pio_uart_driver_config_t;

/** @brief Compile-time PIO assignments indexed by logical port; unused entries are empty. */
extern const pio_uart_driver_config_t pio_uart_driver_configs[UART_PORT_COUNT];

/**
 * @brief Opaque runtime state for one PIO UART instance.
 */
typedef struct pio_uart_driver pio_uart_driver_t;

/**
 * @brief Bind a compile-time PIO descriptor and seed its mutable applied baud rate.
 * @param context Backend instance to configure.
 * @param port_info Immutable entry from the common board table.
 * @return `true` for an inactive instance and its matching board-table entry; failure leaves state unchanged.
 */
bool pio_uart_driver_load_port_config(void *context, const uart_driver_port_info_t *port_info);

/**
 * @brief Initialize one PIO UART backend.
 * @param context Driver instance to initialize.
 * @return `true` when the UART was configured, otherwise `false`.
 */
bool pio_uart_driver_init(void *context);

/**
 * @brief Activate PIO-UART RX DMA interrupts on the UART worker core.
 *
 * Call after all PIO UART backends are initialized and from the core that owns steady-state UART service.
 */
void pio_uart_driver_enable_rx_dma_irq(void);

/**
 * @brief Poll one PIO UART backend to publish RX DMA progress and advance TX service.
 * RX bytes are moved by DMA from the PIO RX FIFO into the shared ring; the poll
 * path publishes producer progress and re-arms exhausted transfers. TX uses FIFO
 * polling for short queues and DMA for deeper backlog.
 * @param context Driver instance to poll.
 * @param tx_launch_allowed False while a control change waits for a TX boundary.
 */
void pio_uart_driver_poll(void *context, bool tx_launch_allowed);

/**
 * @brief Deinitialize one PIO UART backend.
 * @param context Driver instance to deinitialize.
 */
void pio_uart_driver_deinit(void *context);

/**
 * @brief Drop bring-up framing faults before the UART worker starts polling.
 * @param context Driver instance whose startup RX errors should be discarded.
 *
 * Clears a sticky PIO RX stop-bit interrupt without counting it, then zeroes
 * the session framing counter. The first worker poll would otherwise report pin bring-up noise as HID health bit 7.
 */
void pio_uart_driver_clear_rx_error_baseline(void *context);

/**
 * @brief Apply an 8N1 line-coding request to one PIO UART backend.
 * @param context PIO UART runtime instance.
 * @param line_coding Backend-neutral request; non-8N1 formats are rejected.
 * @return `true` when the baud rate was applied, otherwise `false`.
 */
bool pio_uart_driver_set_line_coding(void *context, const uart_driver_line_coding_t *line_coding);

/**
 * @brief Apply a validated baud rate at a safe PIO boundary.
 * @param context PIO UART runtime instance.
 * @param baud_rate Requested baud rate for an 8N1 configuration.
 * @return `true` on successful application, otherwise defer the change.
 * @internal Used by the native backend setter; owns state-machine and DMA sequencing.
 */
bool pio_uart_driver_apply_baud_rate(void *context, uint32_t baud_rate);

#endif
