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

/** @copydoc system_watchdog_enable */
void system_watchdog_enable(void)
{
    watchdog_enable(PICO_UART_WATCHDOG_TIMEOUT_MS, true);
}

/** @copydoc system_watchdog_update */
void system_watchdog_update(void)
{
    watchdog_update();
}

/**
 * @brief Halt after a HardFault so a debugger can inspect the fault.
 *
 * The USB poll loop arms the watchdog with pause-on-debug. A debugger keeps
 * the core stopped here; an unattended board resets when the watchdog expires.
 */
void isr_hardfault(void)
{
    while (true) {
        tight_loop_contents();
    }
}

/** @copydoc system_reset */
void system_reset(void)
{
    watchdog_reboot(0u, 0u, 0u);
}