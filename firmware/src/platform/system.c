/**
 * @file system.c
 * @brief Board-level system-clock setup for PicoUart firmware.
 */

#include "platform/system.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

/** @brief Capture the SDK watchdog distinction before watchdog_enable replaces its scratch marker. */
static bool watchdog_recovery;

/** @copydoc system_init_clock */
void system_init_clock(void)
{
    boot_config_load(watchdog_recovery);
    uint32_t khz = watchdog_recovery ? 0u : boot_config_clock_khz(boot_config_get()->clock_profile);
    if (khz != 0u && !set_sys_clock_khz(khz, false)) {
        boot_config_clock_unavailable();
    }
}

/** @copydoc system_boot_config_status */
const boot_config_status_t *system_boot_config_status(void)
{
    return boot_config_get_status();
}

/** @copydoc system_save_clock_profile */
bool system_save_clock_profile(uint32_t profile)
{
    if (!boot_config_profile_supported(profile)) {
        return false;
    }
    const boot_config_t config = {.clock_profile = (boot_clock_profile_t)profile};
    return boot_config_set(&config) && boot_config_save();
}

/** @copydoc system_watchdog_enable */
void system_watchdog_enable(void)
{
    watchdog_recovery = watchdog_enable_caused_reboot();
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