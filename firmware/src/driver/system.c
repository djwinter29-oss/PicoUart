/**
 * @file system.c
 * @brief Board-level system-clock setup for PicoUart firmware.
 */

#include "driver/system.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

/** @copydoc system_init_clock */
void system_init_clock(void)
{
    hard_assert(set_sys_clock_khz(PICO_UART_SYSTEM_CLOCK_KHZ, true));
}

/** @copydoc system_reset */
void system_reset(void)
{
    watchdog_reboot(0u, 0u, 0u);
}