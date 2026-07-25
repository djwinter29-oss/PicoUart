/**
 * @file line_coding.c
 * @brief Shared UART line-coding validation and USB CDC parsing helpers.
 */

#include "uart/line_coding.h"

bool uart_line_coding_is_valid(const uart_driver_line_coding_t *line_coding)
{
    if ((line_coding == NULL) ||
        (line_coding->baud_rate < UART_LINE_CODING_BAUD_MIN) ||
        (line_coding->baud_rate > UART_LINE_CODING_BAUD_MAX)) {
        return false;
    }

    if ((line_coding->data_bits < 5u) || (line_coding->data_bits > 8u)) {
        return false;
    }

    if ((line_coding->stop_bits != 1u) && (line_coding->stop_bits != 2u)) {
        return false;
    }

    return (line_coding->parity == UART_DRIVER_PARITY_NONE) ||
           (line_coding->parity == UART_DRIVER_PARITY_ODD) ||
           (line_coding->parity == UART_DRIVER_PARITY_EVEN);
}

bool uart_line_coding_pio_supported(const uart_driver_line_coding_t *line_coding)
{
    if (!uart_line_coding_is_valid(line_coding)) {
        return false;
    }

    return (line_coding->data_bits == 8u) &&
           (line_coding->stop_bits == 1u) &&
           (line_coding->parity == UART_DRIVER_PARITY_NONE);
}

bool uart_line_coding_from_usb(uint32_t bit_rate,
                               uint8_t stop_bits,
                               uint8_t parity,
                               uint8_t data_bits,
                               uart_driver_line_coding_t *line_coding)
{
    uart_driver_line_coding_t parsed;

    if (line_coding == NULL) {
        return false;
    }

    parsed.baud_rate = bit_rate;
    parsed.data_bits = data_bits;

    if (stop_bits == 0u) {
        parsed.stop_bits = 1u;
    } else if (stop_bits == 2u) {
        parsed.stop_bits = 2u;
    } else {
        return false;
    }

    if (parity == 0u) {
        parsed.parity = UART_DRIVER_PARITY_NONE;
    } else if (parity == 1u) {
        parsed.parity = UART_DRIVER_PARITY_ODD;
    } else if (parity == 2u) {
        parsed.parity = UART_DRIVER_PARITY_EVEN;
    } else {
        return false;
    }

    if (!uart_line_coding_is_valid(&parsed)) {
        return false;
    }

    *line_coding = parsed;
    return true;
}
