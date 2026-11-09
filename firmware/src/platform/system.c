// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file system.c
 * @brief System clock, watchdog, and reset services for PicoUart firmware.
 */

#include "platform/system.h"
#include "platform/boot_config.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#if PICO_RP2350
#include "hardware/structs/powman.h"
#else
#include "hardware/structs/vreg_and_chip_reset.h"
#endif

/** @brief Scratch marker distinguishing a HardFault followed by watchdog reset. */
#define SYSTEM_HARDFAULT_MARKER 0x50485546u
/** @brief Scratch[1] ownership markers, disjoint from ROM scratch[2..7]. */
#define SYSTEM_STARTED_MARKER 0x50555354u
/** @brief Intentional user reboot marker, consumed at next startup. */
#define SYSTEM_USER_REBOOT_MARKER 0x50555242u
/** @brief Verified configuration-save reboot marker, consumed at next startup. */
#define SYSTEM_CONFIG_SAVE_MARKER 0x50554353u

/** @brief Capture the SDK watchdog distinction before watchdog_enable replaces its scratch marker. */
static bool watchdog_recovery;
/** @brief Evidenced startup cause, captured before changing watchdog state. */
static uint8_t reset_cause;

/**
 * @brief Decode a fresh, single chip-level cause without writing reset-control registers.
 *
 * Chip-level resets clear watchdog scratch; subsystem resets retain it and can
 * leave old chip-level flags. Nonzero scratch[1] therefore disqualifies those
 * flags. On RP2350 chip-level watchdog resets also clear watchdog.reason.
 */
static system_reset_cause_t system_chip_reset_cause(uint32_t previous_marker)
{
    if (previous_marker != 0u) {
        return SYSTEM_RESET_CAUSE_UNKNOWN;
    }
#if PICO_RP2350
    uint32_t cause = powman_hw->chip_reset & (POWMAN_CHIP_RESET_BITS & ~0xffffu);
    if (cause == 0u || (cause & (cause - 1u)) != 0u) {
        return SYSTEM_RESET_CAUSE_UNKNOWN;
    }
    switch (cause) {
    case POWMAN_CHIP_RESET_HAD_POR_BITS:
        return SYSTEM_RESET_CAUSE_POWER_ON;
    case POWMAN_CHIP_RESET_HAD_BOR_BITS:
        return SYSTEM_RESET_CAUSE_BROWNOUT;
    case POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS:
        return SYSTEM_RESET_CAUSE_RUN_PIN;
    case POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS:
    case POWMAN_CHIP_RESET_HAD_RESCUE_BITS:
    case POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS:
        return SYSTEM_RESET_CAUSE_DEBUGGER;
    case POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS:
    case POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS:
    case POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS:
    case POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS:
        return SYSTEM_RESET_CAUSE_WATCHDOG;
    default:
        return SYSTEM_RESET_CAUSE_UNKNOWN;
    }
#else
    uint32_t cause = vreg_and_chip_reset_hw->chip_reset &
        (VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS |
         VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS |
         VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS);
    switch (cause) {
    case VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS:
        return SYSTEM_RESET_CAUSE_POWER_ON_OR_BROWNOUT;
    case VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS:
        return SYSTEM_RESET_CAUSE_RUN_PIN;
    case VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS:
        return SYSTEM_RESET_CAUSE_DEBUGGER;
    default:
        return SYSTEM_RESET_CAUSE_UNKNOWN;
    }
#endif
}

/** @copydoc system_init_clock */
void system_init_clock(void)
{
    boot_config_load(watchdog_recovery);
    uint32_t khz = boot_config_clock_khz(boot_config_get_status()->active_profile);
    if (khz != 0u && !set_sys_clock_khz(khz, false)) {
        boot_config_clock_unavailable();
    }
}

/** @copydoc system_watchdog_enable */
void system_watchdog_enable(void)
{
    bool watchdog_reset = watchdog_caused_reboot();
    bool hardfault_reset = watchdog_reset && (watchdog_hw->scratch[0] == SYSTEM_HARDFAULT_MARKER);
    uint32_t marker = watchdog_hw->scratch[1];
    uint32_t reason = watchdog_hw->reason;

    watchdog_hw->scratch[0] = 0u;
    watchdog_hw->scratch[1] = SYSTEM_STARTED_MARKER;
    watchdog_recovery = watchdog_enable_caused_reboot();
    if (hardfault_reset) {
        reset_cause = SYSTEM_RESET_CAUSE_HARDFAULT;
    } else if (watchdog_reset && reason == WATCHDOG_REASON_FORCE_BITS) {
        reset_cause = marker == SYSTEM_USER_REBOOT_MARKER ? SYSTEM_RESET_CAUSE_USER_REBOOT :
                      marker == SYSTEM_CONFIG_SAVE_MARKER ? SYSTEM_RESET_CAUSE_CONFIG_SAVE :
                      SYSTEM_RESET_CAUSE_SOFTWARE;
    } else if (watchdog_reset) {
        reset_cause = SYSTEM_RESET_CAUSE_WATCHDOG;
    } else {
        /* A non-normal RP2350 ROM boot can invalidate the SDK watchdog evidence. */
        reset_cause = reason != 0u ? SYSTEM_RESET_CAUSE_UNKNOWN : system_chip_reset_cause(marker);
    }
    watchdog_enable(PICO_UART_WATCHDOG_TIMEOUT_MS, true);
}

/** @copydoc system_reset_cause */
uint8_t system_reset_cause(void)
{
    return reset_cause;
}

/** @copydoc system_note_hardfault */
void system_note_hardfault(void)
{
    watchdog_hw->scratch[0] = SYSTEM_HARDFAULT_MARKER;
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
    system_note_hardfault();
    while (true) {
        tight_loop_contents();
    }
}

/** @copydoc system_reset */
void system_reset(system_reset_cause_t cause)
{
    watchdog_hw->scratch[1] = cause == SYSTEM_RESET_CAUSE_USER_REBOOT ? SYSTEM_USER_REBOOT_MARKER :
                            cause == SYSTEM_RESET_CAUSE_CONFIG_SAVE ? SYSTEM_CONFIG_SAVE_MARKER : 0u;
    watchdog_reboot(0u, 0u, 0u);
}