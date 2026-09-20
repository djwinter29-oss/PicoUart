/**
 * @file baud_rate.h
 * @brief PL011 baud-divisor validation shared by hardware UART control paths.
 */

#ifndef HW_UART_BAUD_RATE_H
#define HW_UART_BAUD_RATE_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum allowed hardware UART baud-rate error in parts per million. */
#define HW_UART_BAUD_RATE_MAX_ERROR_PPM 20000u
/** @brief Largest PL011 integer-plus-fractional baud divisor encoded in 1/64 units. */
#define HW_UART_BAUD_RATE_DIVISOR_MAX ((65535u * 64u) + 63u)

/**
 * @brief Calculate the nearest representable PL011 baud rate.
 * @param requested_rate Requested line rate in bits per second.
 * @param peripheral_hz UART peripheral clock in Hz.
 * @param actual_rate Output storage for the nearest representable rate.
 * @param error_ppm Output storage for the absolute rate error in ppm.
 * @return `true` when the requested rate has a legal PL011 divisor.
 */
static inline bool hw_uart_baud_rate_calculate(uint32_t requested_rate,
                                               uint32_t peripheral_hz,
                                               uint32_t *actual_rate,
                                               uint32_t *error_ppm)
{
    uint64_t divisor;
    uint64_t actual;
    uint64_t difference;

    if ((requested_rate == 0u) || (peripheral_hz == 0u) ||
        (actual_rate == NULL) || (error_ppm == NULL)) {
        return false;
    }

    divisor = (((uint64_t)peripheral_hz * 4u) + ((uint64_t)requested_rate / 2u)) /
              (uint64_t)requested_rate;
    if ((divisor == 0u) || (divisor > HW_UART_BAUD_RATE_DIVISOR_MAX)) {
        return false;
    }

    actual = ((uint64_t)peripheral_hz * 4u + (divisor / 2u)) / divisor;
    difference = (actual > requested_rate) ? (actual - requested_rate) :
                 ((uint64_t)requested_rate - actual);
    *actual_rate = (uint32_t)actual;
    *error_ppm = (uint32_t)((difference * 1000000u + ((uint64_t)requested_rate / 2u)) /
                            (uint64_t)requested_rate);
    return true;
}

/**
 * @brief Return whether a requested hardware UART rate is within the supported error tolerance.
 */
static inline bool hw_uart_baud_rate_supported(uint32_t requested_rate,
                                               uint32_t peripheral_hz,
                                               uint32_t *actual_rate)
{
    uint32_t error_ppm;

    return hw_uart_baud_rate_calculate(requested_rate, peripheral_hz, actual_rate, &error_ppm) &&
           (error_ppm <= HW_UART_BAUD_RATE_MAX_ERROR_PPM);
}

#endif