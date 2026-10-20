/**
 * @file system.c
 * @brief Board-level system-clock setup for PicoUart firmware.
 */

#include "driver/system.h"

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include <string.h>

_Static_assert(PICO_FLASH_SIZE_BYTES == PICO_UART_CONFIG_FLASH_SIZE, "Configuration must use physical board capacity");
_Static_assert(PICO_UART_CONFIG_FLASH_OFFSET + BOARD_CONFIG_SECTOR_SIZE == PICO_FLASH_SIZE_BYTES,
               "Configuration must occupy the final flash sector");

/** @brief Boot-only decision; immutable once UART/USB services start. */
static board_config_status_t config_status;
/** @brief Capture the SDK watchdog distinction before watchdog_enable replaces its scratch marker. */
static bool watchdog_recovery;

/** @brief SDK-selected target family used for both boot validation and writes. */
static const bool system_rp2350 =
#if defined(PICO_RP2350A) || defined(PICO_RP2350B) || (defined(PICO_RP2350) && PICO_RP2350)
        true;
#else
        false;
#endif

/** @copydoc system_init_clock */
void system_init_clock(void)
{
    const uint8_t *sector = (const uint8_t *)(XIP_BASE + PICO_UART_CONFIG_FLASH_OFFSET);
    config_status = board_config_decode(sector, BOARD_CONFIG_SECTOR_SIZE, system_rp2350, watchdog_recovery);
    uint32_t khz = board_config_clock_khz(config_status.active_profile);
    if (khz != 0u && !set_sys_clock_khz(khz, false)) {
        config_status.active_profile = BOARD_CLOCK_DEFAULT;
        config_status.source = BOARD_CONFIG_DEFAULTS;
        config_status.reason = BOARD_CONFIG_CLOCK_UNAVAILABLE;
    }
}

/** @copydoc system_config_status */
const board_config_status_t *system_config_status(void)
{
    return &config_status;
}

/**
 * @brief Erase only the reserved sector and program its first page while XIP is unavailable.
 * @param page SRAM page prepared before multicore lockout.
 */
static void __not_in_flash_func(system_program_config)(void *page)
{
    flash_range_erase(PICO_UART_CONFIG_FLASH_OFFSET, BOARD_CONFIG_SECTOR_SIZE);
    flash_range_program(PICO_UART_CONFIG_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
}

/** @copydoc system_save_clock_profile */
bool system_save_clock_profile(uint32_t profile)
{
    if (!board_config_profile_supported(profile, system_rp2350)) {
        return false;
    }
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    board_config_encode(page, (board_clock_profile_t)profile);
    const uint8_t *sector = (const uint8_t *)(XIP_BASE + PICO_UART_CONFIG_FLASH_OFFSET);
    board_config_status_t stored = board_config_decode(sector, BOARD_CONFIG_SECTOR_SIZE, system_rp2350, false);
    if (stored.reason == BOARD_CONFIG_OK && stored.stored_profile == profile) {
        return true;
    }
    if (flash_safe_execute(system_program_config, page, 1000u) != PICO_OK) {
        return false;
    }
    if (memcmp(sector, page, sizeof(page)) != 0) {
        return false;
    }
    stored = board_config_decode(sector, BOARD_CONFIG_SECTOR_SIZE, system_rp2350, false);
    return stored.reason == BOARD_CONFIG_OK && stored.stored_profile == profile;
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