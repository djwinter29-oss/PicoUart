/**
 * @file system.c
 * @brief Board-level system-clock setup for PicoUart firmware.
 */

#include "driver/system.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"

/** @copydoc system_init_clock */
void system_init_clock(void)
{
    hard_assert(set_sys_clock_khz(PICO_UART_SYSTEM_CLOCK_KHZ, true));
}