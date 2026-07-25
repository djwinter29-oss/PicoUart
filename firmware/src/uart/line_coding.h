/**
 * @file line_coding.h
 * @brief Shared UART line-coding validation and USB CDC parsing helpers.
 *
 * Hosted unit tests build this module without the Pico SDK. Keep it free of
 * hardware headers.
 */

#ifndef UART_LINE_CODING_H
#define UART_LINE_CODING_H

#include "uart/uart_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Minimum accepted host baud rate (inclusive). */
#define UART_LINE_CODING_BAUD_MIN 50u
/**
 * @brief Maximum accepted host baud rate (inclusive).
 *
 * Backends may still reject rates their clock divider cannot represent.
 */
#define UART_LINE_CODING_BAUD_MAX 3000000u

/**
 * @brief Return whether @p line_coding is structurally valid for any backend.
 * @param line_coding Host-requested baud/data/parity/stop configuration.
 * @return `true` when the request is within shared firmware bounds.
 */
bool uart_line_coding_is_valid(const uart_driver_line_coding_t *line_coding);

/**
 * @brief Return whether @p line_coding is supported by the PIO UART backend.
 * @param line_coding Host-requested baud/data/parity/stop configuration.
 * @return `true` only for 8N1 requests that also pass @ref uart_line_coding_is_valid.
 */
bool uart_line_coding_pio_supported(const uart_driver_line_coding_t *line_coding);

/**
 * @brief Translate a USB CDC ACM line-coding request into firmware form.
 * @param bit_rate USB `dwDTERate` baud rate.
 * @param stop_bits USB stop-bit code (`0` = 1, `1` = 1.5, `2` = 2).
 * @param parity USB parity code (`0` none, `1` odd, `2` even, others rejected).
 * @param data_bits USB data-bit count.
 * @param line_coding Output storage for the translated request.
 * @return `true` when the USB request maps to a valid firmware line-coding.
 *
 * USB 1.5 stop bits and mark/space parity are rejected. A successful USB
 * `SET_LINE_CODING` transfer does not guarantee the matching UART backend will
 * apply the request; PIO ports remain 8N1-only and surface rejects through
 * @ref UART_DRIVER_PORT_STATUS_CONTROL_ERROR.
 */
bool uart_line_coding_from_usb(uint32_t bit_rate,
                               uint8_t stop_bits,
                               uint8_t parity,
                               uint8_t data_bits,
                               uart_driver_line_coding_t *line_coding);

#endif
