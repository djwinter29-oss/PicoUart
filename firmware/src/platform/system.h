// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file system.h
 * @brief System clock, watchdog, and reset services for PicoUart firmware.
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

/** @brief Reset causes reported through the board-configuration HID report. */
typedef enum {
    SYSTEM_RESET_CAUSE_UNKNOWN = 0,   /**< Cause not reliably distinguished. */
    SYSTEM_RESET_CAUSE_WATCHDOG = 2,  /**< Watchdog reset without a more specific marker. */
    SYSTEM_RESET_CAUSE_SOFTWARE = 3,  /**< Software-requested reset when independently evidenced. */
    SYSTEM_RESET_CAUSE_HARDFAULT = 4, /**< Watchdog recovery after a recorded HardFault. */
} system_reset_cause_t;

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
 * @return HID reset cause; power-on and software reset are not inferred.
 */
uint8_t system_reset_cause(void);

/**
 * @brief Record that a HardFault occurred before the watchdog recovery reset.
 *
 * The marker uses watchdog scratch storage and does not write flash.
 */
void system_note_hardfault(void);

/**
 * @brief Reboot the board immediately through the watchdog.
 */
void system_reset(void);

#endif