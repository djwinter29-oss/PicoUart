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
    SYSTEM_RESET_CAUSE_UNKNOWN = 0,         /**< Cause not reliably distinguished. */
    SYSTEM_RESET_CAUSE_POWER_ON = 1,        /**< RP2350 power-on reset, distinct from brownout. */
    SYSTEM_RESET_CAUSE_WATCHDOG = 2,        /**< Watchdog reset without a more specific cause. */
    SYSTEM_RESET_CAUSE_SOFTWARE = 3,        /**< Software-triggered watchdog reset without an application marker. */
    SYSTEM_RESET_CAUSE_HARDFAULT = 4,       /**< Watchdog recovery after a recorded HardFault. */
    SYSTEM_RESET_CAUSE_POWER_ON_OR_BROWNOUT = 5, /**< RP2040 combined power-on/brownout indication. */
    SYSTEM_RESET_CAUSE_BROWNOUT = 6,        /**< RP2350 brownout reset. */
    SYSTEM_RESET_CAUSE_RUN_PIN = 7,         /**< Chip-level reset from the RUN pin. */
    SYSTEM_RESET_CAUSE_DEBUGGER = 8,        /**< Evidenced chip-level debugger reset, not arbitrary core restart. */
    SYSTEM_RESET_CAUSE_USER_REBOOT = 9,     /**< Marked software-triggered watchdog reboot requested by the user. */
    SYSTEM_RESET_CAUSE_CONFIG_SAVE = 10,    /**< Marked software-triggered watchdog reboot after verified config save. */
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
 * Captures reset evidence and consumes application reset markers before arming.
 */
void system_watchdog_enable(void);

/**
 * @brief Pet the watchdog from the USB poll loop when the UART worker is alive.
 */
void system_watchdog_update(void);

/**
 * @brief Return the evidenced cause captured before watchdog initialization.
 * @return HID reset cause, or Unknown for absent, conflicting, or stale chip-level evidence.
 */
uint8_t system_reset_cause(void);

/**
 * @brief Record that a HardFault occurred before the watchdog recovery reset.
 *
 * The marker owns watchdog scratch[0] and does not write flash.
 */
void system_note_hardfault(void);

/**
 * @brief Reboot the board immediately through the watchdog, recording the request in scratch[1].
 * @param cause User reboot or configuration-save reboot; other values use the software-requested fallback.
 *
 * Scratch[1] also records that startup ran, preventing retained chip-level flags
 * from being mistaken for a new reset. Scratch[2..3] (ROM reboot parameters) and
 * scratch[4..7] (SDK watchdog/ROM boot vector) are left to their owners.
 */
void system_reset(system_reset_cause_t cause);

#endif