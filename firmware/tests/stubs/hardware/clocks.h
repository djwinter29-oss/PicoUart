/** @file clocks.h
 * @brief Host-test declarations for the SDK system clock query.
 */
#ifndef HARDWARE_CLOCKS_H
#define HARDWARE_CLOCKS_H

#include <stdint.h>

/** @brief System clock selector used by the HID hardware feature. */
#define clk_sys 0u
/** @brief Peripheral clock selector used by the native HW UART backend. */
#define clk_peri 1u

/** @brief Return the fixture clock rate. */
uint32_t clock_get_hz(unsigned int clock);

#endif
