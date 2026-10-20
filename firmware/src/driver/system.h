/**
 * @file system.h
 * @brief Board-level system-clock setup for PicoUart firmware.
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include "driver/board_config.h"

/** @brief Watchdog timeout that covers USB poll plus a 1 s line-coding apply. */
#ifndef PICO_UART_WATCHDOG_TIMEOUT_MS
#define PICO_UART_WATCHDOG_TIMEOUT_MS 8000u
#endif

/**
 * @brief Configure the system clock before initializing timing-sensitive hardware.
 *
 * Load the CRC-protected configuration. Default, rejected settings and watchdog
 * recovery leave SDK/board clocks untouched. Only a valid non-default profile
 * calls `set_sys_clock_khz`; core voltage is never written. Non-default profiles
 * require exact-board HIL before any stability claim.
 */
void system_init_clock(void);

/**
 * @brief Read the immutable boot configuration decision.
 * @return Static RAM snapshot valid after system_init_clock.
 */
const board_config_status_t *system_config_status(void);

/**
 * @brief Save and verify a supported clock profile without changing the running clock.
 * @param profile Target-compatible permanent profile ID.
 * @return True only after full-sector readback verification (or an identical valid record).
 * @note Call on core 0 with UART traffic stopped and the core-1 flash lockout victim initialized.
 */
bool system_save_clock_profile(uint32_t profile);

/**
 * @brief Arm the watchdog so a wedged USB poll loop recovers without a power cycle.
 *
 * Pauses the watchdog while a debugger is attached so HardFault can still be
 * inspected. Without a debugger, `isr_hardfault` spins until this timeout resets.
 */
void system_watchdog_enable(void);

/**
 * @brief Pet the watchdog from the USB poll loop when the UART worker is alive.
 */
void system_watchdog_update(void);

/**
 * @brief Reboot the board immediately through the watchdog.
 */
void system_reset(void);

#endif