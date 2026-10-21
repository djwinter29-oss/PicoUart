/**
 * @file hw_uart_driver.h
 * @brief Hardware UART backend for PicoUart logical UART ports.
 */

#ifndef HW_UART_DRIVER_H
#define HW_UART_DRIVER_H

#include "config/capacity_config.h"
#include "hardware/uart.h"
#include "uart/ring_buffer/ring_buffer.h"
#include "uart/uart_driver.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Static configuration for one hardware UART instance.
 */
typedef struct {
    const uart_driver_port_info_t *port; /**< Immutable board wiring and startup line coding. */
    uart_inst_t *instance;      /**< RP2040/RP2350 hardware UART peripheral. */
    bool hardware_flow_control; /**< Enable CTS/RTS when both flow-control pins are assigned. */
} hw_uart_driver_config_t;

/** @brief Compile-time HW assignments indexed by logical port; unused entries are empty. */
extern const hw_uart_driver_config_t hw_uart_driver_configs[UART_PORT_COUNT];

/**
 * @brief Runtime state for one hardware UART instance.
 */
typedef struct {
    const hw_uart_driver_config_t *config; /**< Immutable backend descriptor and board wiring. */
    uint32_t baud_rate; /**< Applied hardware baud rate. */
    uint8_t data_bits; /**< Applied data-bit count. */
    uint8_t stop_bits; /**< Applied stop-bit count. */
    uart_parity_t parity; /**< Applied hardware parity mode. */
    bool initialized;                       /**< True after the peripheral has been configured. */
    int rx_dma_channel;                     /**< Claimed DMA channel used for UART RX. */
    int tx_dma_channel;                     /**< Claimed DMA channel used for UART TX. */
    size_t tx_dma_bytes_in_flight;          /**< Bytes currently owned by the active TX DMA transfer. */
    bool tx_active;                         /**< True while a TX DMA transfer is still in flight. */
    bool rx_rts_asserted;                   /**< True while manual RTS permits the peer to transmit. */
    uint32_t controller_tx_bytes;           /**< Bytes completed by the UART TX DMA engine. */
    uint32_t controller_rx_bytes;           /**< Bytes accepted by the UART RX DMA engine. */
    uint32_t rx_error_count;                /**< Hardware UART receive-status events observed since initialization. */
    volatile uint32_t rx_dma_last_progress; /**< Last current-transfer RX DMA progress used for accounting. */
    ring_buffer_t rx_ring;                  /**< UART-to-USB receive ring. */
    ring_buffer_t tx_ring;                  /**< USB-to-UART transmit ring. */
    uint8_t rx_storage[PICO_UART_HW_UART_RX_BUFFER_SIZE]
        __attribute__((aligned(PICO_UART_HW_UART_RX_BUFFER_SIZE))); /**< DMA-owned RX ring storage. */
    uint8_t tx_storage[PICO_UART_HW_UART_TX_BUFFER_SIZE];           /**< TX ring storage. */
} hw_uart_driver_t;

/**
 * @brief Bind a compile-time HW descriptor and seed mutable applied line coding.
 * @param context Backend instance to configure.
 * @param port_info Immutable entry from the common board table.
 * @return `true` for an inactive instance and its matching board-table entry; failure leaves state unchanged.
 */
bool hw_uart_driver_load_port_config(void *context, const uart_driver_port_info_t *port_info);

/**
 * @brief Initialize one hardware UART backend.
 * @param context Driver instance to initialize.
 * @return `true` when the UART was configured, otherwise `false`.
 */
bool hw_uart_driver_init(void *context);

/**
 * @brief Activate hardware-UART RX DMA interrupts on the UART worker core.
 *
 * Call after all hardware UART backends are initialized and from the core that owns steady-state UART service.
 */
void hw_uart_driver_enable_rx_dma_irq(void);

/**
 * @brief Clear receive-status errors accumulated during board bring-up.
 * @param context Initialized hardware UART backend to baseline.
 *
 * Call after every configured backend has started so transient startup line
 * activity is not reported as a runtime receive error.
 */
void hw_uart_driver_clear_rx_error_baseline(void *context);

/**
 * @brief Poll one hardware UART backend to advance TX DMA completion state.
 * @param context Driver instance to poll.
 * @param tx_launch_allowed False while a control change waits for a TX boundary.
 */
void hw_uart_driver_poll(void *context, bool tx_launch_allowed);

/**
 * @brief Deinitialize one hardware UART backend.
 * @param context Driver instance to deinitialize.
 */
void hw_uart_driver_deinit(void *context);

/**
 * @brief Reconfigure the full UART line format for one hardware UART backend.
 * @param context Hardware UART runtime instance.
 * @param line_coding Backend-neutral requested baud and framing.
 * @return `true` when the line format was applied, otherwise `false`.
 */
bool hw_uart_driver_set_line_coding(void *context, const uart_driver_line_coding_t *line_coding);

/**
 * @brief Apply validated SDK-format line coding at a safe hardware boundary.
 * @param context Hardware UART runtime instance.
 * @param baud_rate Requested baud rate.
 * @param data_bits Requested data-bit count.
 * @param stop_bits Requested stop-bit count.
 * @param parity Validated SDK parity encoding.
 * @return `true` on successful application; otherwise defer or roll back.
 * @internal Used by the native backend setter; owns register and DMA sequencing.
 */
bool hw_uart_driver_apply_line_format(void *context, uint32_t baud_rate, uint8_t data_bits, uint8_t stop_bits,
                                     uart_parity_t parity);

/**
 * @brief Check a consumer reservation against the live RX DMA write position.
 * @param context Hardware UART backend to inspect.
 * @param consumer_sequence Consumer sequence captured for the RX span.
 * @return `true` when live DMA progress has not overwritten the reservation.
 */
bool hw_uart_driver_rx_snapshot_is_current(const void *context, uint32_t consumer_sequence);

#endif
