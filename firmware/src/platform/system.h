/**
 * @file system.h
 * @brief System clock, watchdog, and reset services for PicoUart firmware.
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

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
 * @brief Return the evidenced cause captured before watchdog initialization.
 * @return HID reset cause: zero Unknown, or two Watchdog; power-on is not inferred.
 */
uint8_t system_reset_cause(void);

/**
 * @brief Reboot the board immediately through the watchdog.
 */
void system_reset(void);

#endif