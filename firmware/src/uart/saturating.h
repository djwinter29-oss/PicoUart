/**
 * @file saturating.h
 * @brief Saturating arithmetic helpers for UART diagnostics.
 */

#ifndef UART_SATURATING_H
#define UART_SATURATING_H

#include <stdint.h>

/**
 * @brief Add two unsigned 32-bit values, saturating at UINT32_MAX.
 * @param value Existing cumulative value.
 * @param increment Value to add.
 * @return The exact sum when representable, otherwise UINT32_MAX.
 */
static inline uint32_t uart_saturating_add_u32(uint32_t value, uint32_t increment)
{
    return (increment > UINT32_MAX - value) ? UINT32_MAX : value + increment;
}

#endif