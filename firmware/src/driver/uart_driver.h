/**
 * @file uart_driver.h
 * @brief Logical UART port table for the PicoUart firmware.
 */

#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Backend class used by a logical UART port.
 */
typedef enum {
    UART_DRIVER_BACKEND_HW = 0, /**< Hardware UART peripheral backend. */
    UART_DRIVER_BACKEND_PIO, /**< PIO UART backend. */
} uart_driver_backend_t;

/**
 * @brief Result code returned by cross-core UART control commands.
 */
typedef enum {
    UART_DRIVER_COMMAND_STATUS_OK = 0, /**< Command completed successfully. */
    UART_DRIVER_COMMAND_STATUS_WORKER_NOT_STARTED = 1, /**< UART worker core has not started yet. */
    UART_DRIVER_COMMAND_STATUS_INVALID_PORT = 2, /**< Command referenced an invalid logical port. */
    UART_DRIVER_COMMAND_STATUS_INVALID_ARGUMENT = 3, /**< Command arguments were malformed or unsupported. */
    UART_DRIVER_COMMAND_STATUS_INIT_FAILED = 4, /**< One or more UART backends failed during init. */
    UART_DRIVER_COMMAND_STATUS_BACKEND_REJECTED = 5, /**< Backend rejected the requested control change. */
    UART_DRIVER_COMMAND_STATUS_UNSUPPORTED = 6, /**< Command could not be applied to the selected backend. */
    UART_DRIVER_COMMAND_STATUS_QUEUED = 7, /**< Command was accepted and queued for deferred worker-side apply. */
    UART_DRIVER_COMMAND_STATUS_TIMEOUT = 8, /**< Worker did not complete the command before its deadline. */
} uart_driver_command_status_t;

/** @brief Port status flag: backend is initialized and available. */
#define UART_DRIVER_PORT_STATUS_READY (1u << 0)
/** @brief Port status flag: backend init failed for this port. */
#define UART_DRIVER_PORT_STATUS_INIT_FAILED (1u << 1)
/** @brief Port status flag: the most recent control command for this port failed. */
#define UART_DRIVER_PORT_STATUS_CONTROL_ERROR (1u << 2)
/** @brief Port status flag: the worker core is currently applying a control change for this port. */
#define UART_DRIVER_PORT_STATUS_CONTROL_PENDING (1u << 3)

/**
 * @brief Logical UART port identifiers.
 */
typedef enum {
    UART_PORT_0 = 0, /**< Logical UART port 0. */
    UART_PORT_1, /**< Logical UART port 1. */
    UART_PORT_2, /**< Logical UART port 2. */
    UART_PORT_3, /**< Logical UART port 3. */
    UART_PORT_4, /**< Logical UART port 4. */
    UART_PORT_5, /**< Logical UART port 5. */
    UART_PORT_COUNT /**< Total number of logical UART ports. */
} uart_port_id_t;

/**
 * @brief Public view of one logical UART port.
 */
typedef struct {
    uart_port_id_t id; /**< Logical port identifier. */
    uart_driver_backend_t backend; /**< Backend class assigned to the port. */
    uint32_t baud_rate; /**< Configured startup baud rate. */
    uint32_t tx_pin; /**< Configured TX GPIO, or unassigned marker. */
    uint32_t rx_pin; /**< Configured RX GPIO, or unassigned marker. */
} uart_driver_port_info_t;

/**
 * @brief Host-visible PIO observability counters for one logical port.
 */
typedef struct {
    uint32_t rx_framing_error_count; /**< Full count of dropped RX words with an invalid stop bit. */
    uint32_t tx_dma_claim_failure_count; /**< Full count of failed TX DMA channel claims. */
    uint32_t tx_polled_bytes; /**< Full count of bytes sent through the poll path. */
    uint32_t tx_dma_bytes; /**< Full count of bytes sent through the DMA path. */
} uart_driver_pio_stats_t;

/**
 * @brief Host-requested UART parity mode.
 */
typedef enum {
    UART_DRIVER_PARITY_NONE = 0, /**< No parity bit. */
    UART_DRIVER_PARITY_ODD, /**< Odd parity. */
    UART_DRIVER_PARITY_EVEN, /**< Even parity. */
} uart_driver_parity_t;

/**
 * @brief Worker-applied UART line-coding request.
 */
typedef struct {
    uint32_t baud_rate; /**< Requested baud rate. */
    uint8_t data_bits; /**< Requested UART data-bit count. */
    uint8_t stop_bits; /**< Requested UART stop-bit count. */
    uart_driver_parity_t parity; /**< Requested parity mode. */
} uart_driver_line_coding_t;

/**
 * @brief Return the number of logical UART ports in the firmware.
 * @return Always 6 for the current design.
 */
size_t uart_driver_port_count(void);

/**
 * @brief Initialize all configured UART backend instances.
 * @return `true` when every configured backend initialized successfully, otherwise `false`.
 */
bool uart_driver_init(void);

/**
 * @brief Return whether one logical UART port is initialized and available.
 * @param port_id Logical port identifier.
 * @return `true` when the port is active, otherwise `false`.
 */
bool uart_driver_port_is_ready(uart_port_id_t port_id);

/**
 * @brief Advance DMA state for the two hardware UART backends.
 */
void uart_driver_poll_hardware(void);

/**
 * @brief Advance RX and TX state for the four PIO UART backends.
 */
void uart_driver_poll_pio(void);

/**
 * @brief Read bytes from one logical UART port RX ring.
 * @param port_id Logical port identifier.
 * @param data Destination buffer.
 * @param capacity Maximum bytes to read.
 * @return Number of bytes copied out of the shared RX ring.
 */
size_t uart_driver_read(uart_port_id_t port_id, uint8_t *data, size_t capacity);

/**
 * @brief Return the currently available shared TX-ring space for one logical UART port.
 * @param port_id Logical port identifier.
 * @return Number of bytes that can currently be queued for transmission.
 */
size_t uart_driver_write_available(uart_port_id_t port_id);

/**
 * @brief Reconfigure one logical UART port from a host line-coding request.
 * @param port_id Logical port identifier.
 * @param line_coding Requested baud/data/parity/stop configuration.
 * @return `true` when the line coding was applied, otherwise `false`.
 */
bool uart_driver_set_line_coding(uart_port_id_t port_id,
                                 const uart_driver_line_coding_t *line_coding);

/**
 * @brief Queue one host line-coding request without waiting for the UART worker.
 * @param port_id Logical port identifier.
 * @param line_coding Requested baud/data/parity/stop configuration.
 * @return `true` when the worker mailbox accepted the request, otherwise `false`.
 */
bool uart_driver_queue_line_coding(uart_port_id_t port_id,
                                   const uart_driver_line_coding_t *line_coding);

/**
 * @brief Reconfigure the baud rate for one logical UART port.
 * @param port_id Logical port identifier.
 * @param baud_rate New baud rate.
 * @return `true` when the baud rate was applied, otherwise `false`.
 */
bool uart_driver_set_baud_rate(uart_port_id_t port_id, uint32_t baud_rate);

/**
 * @brief Return the status flags for one logical UART port.
 * @param port_id Logical port identifier.
 * @return Bitwise OR of @ref UART_DRIVER_PORT_STATUS_* flags.
 */
uint8_t uart_driver_port_status(uart_port_id_t port_id);

/**
 * @brief Return the most recent cross-core control status.
 * @return Latest mailbox command result.
 */
uart_driver_command_status_t uart_driver_last_command_status(void);

/**
 * @brief Return the logical port associated with the most recent control status.
 * @return Port index, or `UART_PORT_COUNT` when the last command was not port-specific.
 */
uart_port_id_t uart_driver_last_command_port(void);

/**
 * @brief Return whether the dedicated UART worker core is running.
 * @return `true` after the worker core has been launched.
 */
bool uart_driver_worker_is_running(void);

/**
 * @brief Return the UART port currently being serviced by the worker.
 * @return Logical port index, or @ref UART_PORT_COUNT between poll passes.
 */
uart_port_id_t uart_driver_worker_poll_port(void);

/**
 * @brief Return the worker poll-loop heartbeat.
 * @return Value incremented after each completed backend poll sweep.
 */
uint8_t uart_driver_worker_heartbeat(void);

/**
 * @brief Return the core that most recently entered HardFault.
 * @return Zero when no fault occurred, one for core 0, or two for core 1.
 */
uint8_t uart_driver_hardfault_core(void);

/**
 * @brief Write bytes into one logical UART port TX ring.
 * @param port_id Logical port identifier.
 * @param data Source bytes.
 * @param length Number of bytes to queue.
 * @return Number of bytes accepted into the shared TX ring.
 */
size_t uart_driver_write(uart_port_id_t port_id, const uint8_t *data, size_t length);

/**
 * @brief Return public metadata for one logical UART port.
 * @param port_id Logical port identifier.
 * @return Pointer to immutable port metadata, or `NULL` when the index is invalid.
 */
const uart_driver_port_info_t *uart_driver_port_info(uart_port_id_t port_id);

/**
 * @brief Return compact PIO counters for one logical UART port.
 * @param port_id Logical port identifier.
 * @param stats Output storage for the compact counters.
 * @return `true` when the port uses the PIO backend and stats were written, otherwise `false`.
 */
bool uart_driver_port_pio_stats(uart_port_id_t port_id, uart_driver_pio_stats_t *stats);

/**
 * @brief Check that the logical UART table still matches the 2-HW and 4-PIO design.
 * @return `true` when the table is valid, otherwise `false`.
 */
bool uart_driver_validate_topology(void);

#endif