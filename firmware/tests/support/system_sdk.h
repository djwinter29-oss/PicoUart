// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/** @file system_sdk.h
 * @brief Native SDK declarations and reset masks from the SDK's generated register headers.
 */
#ifndef TEST_SYSTEM_SDK_H
#define TEST_SYSTEM_SDK_H

#include <stdbool.h>
#include <stdint.h>
/* Keep native tests independent of a Pico SDK installation. */
#define WATCHDOG_REASON_FORCE_BITS 0x00000002u
#define WATCHDOG_REASON_TIMER_BITS 0x00000001u
#if PICO_RP2350
#define POWMAN_CHIP_RESET_BITS 0x1fef0011u
#define POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS 0x10000000u
#define POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS 0x08000000u
#define POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS 0x04000000u
#define POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS 0x02000000u
#define POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS 0x01000000u
#define POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS 0x00800000u
#define POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS 0x00400000u
#define POWMAN_CHIP_RESET_HAD_RESCUE_BITS 0x00200000u
#define POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS 0x00080000u
#define POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS 0x00040000u
#define POWMAN_CHIP_RESET_HAD_BOR_BITS 0x00020000u
#define POWMAN_CHIP_RESET_HAD_POR_BITS 0x00010000u
#define POWMAN_CHIP_RESET_RESCUE_FLAG_BITS 0x00000010u
#define POWMAN_CHIP_RESET_DOUBLE_TAP_BITS 0x00000001u
#else
#define VREG_AND_CHIP_RESET_CHIP_RESET_PSM_RESTART_FLAG_BITS 0x01000000u
#define VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS 0x00100000u
#define VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS 0x00010000u
#define VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS 0x00000100u
#endif

/** @brief Watchdog registers used by system.c. */
typedef struct {
    uint32_t reason; /**< Latched watchdog reason. */
    uint32_t scratch[8]; /**< Retained application and SDK words. */
} test_watchdog_hw_t;
/** @brief Chip-level reset register used by system.c. */
typedef struct {
    uint32_t chip_reset; /**< Last chip-level reset source. */
} test_chip_reset_hw_t;
extern test_watchdog_hw_t test_watchdog_hw;
extern test_chip_reset_hw_t test_chip_reset_hw;
#define watchdog_hw (&test_watchdog_hw)
#define powman_hw (&test_chip_reset_hw)
#define vreg_and_chip_reset_hw (&test_chip_reset_hw)

bool watchdog_caused_reboot(void);
bool watchdog_enable_caused_reboot(void);
void watchdog_enable(uint32_t timeout_ms, bool pause_on_debug);
void watchdog_update(void);
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);
bool set_sys_clock_khz(uint32_t khz, bool required);
void tight_loop_contents(void);

#endif
