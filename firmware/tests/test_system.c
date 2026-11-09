// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/** @file test_system.c
 * @brief Exercise production reset classification and scratch ownership on both platforms.
 */
#include "unity.h"
#include "../src/platform/system.c"
#include <string.h>

test_watchdog_hw_t test_watchdog_hw;
test_chip_reset_hw_t test_chip_reset_hw;
static bool normal_boot, loaded_recovery;
static unsigned reboot_calls;

bool watchdog_caused_reboot(void) { return watchdog_hw->reason != 0u && normal_boot; }
bool watchdog_enable_caused_reboot(void)
{
    return watchdog_hw->reason != 0u && watchdog_hw->scratch[4] == 0x6ab73121u;
}
void watchdog_enable(uint32_t timeout_ms, bool pause_on_debug)
{
    TEST_ASSERT_EQUAL_UINT32(PICO_UART_WATCHDOG_TIMEOUT_MS, timeout_ms);
    TEST_ASSERT_TRUE(pause_on_debug);
    watchdog_hw->scratch[4] = 0x6ab73121u;
}
void watchdog_update(void) {}
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms)
{
    TEST_ASSERT_EQUAL_UINT32(0u, pc | sp | delay_ms);
    watchdog_hw->scratch[4] = 0u;
    watchdog_hw->reason = WATCHDOG_REASON_FORCE_BITS;
    ++reboot_calls;
}
bool set_sys_clock_khz(uint32_t khz, bool required) { (void)khz; (void)required; return true; }
void tight_loop_contents(void) {}
void boot_config_load(bool recovery) { loaded_recovery = recovery; }
uint32_t boot_config_clock_khz(boot_clock_profile_t profile) { (void)profile; return 0u; }
const boot_config_status_t *boot_config_get_status(void)
{
    static const boot_config_status_t status = {0};
    return &status;
}
void boot_config_clock_unavailable(void) {}

void setUp(void)
{
    memset(&test_watchdog_hw, 0, sizeof(test_watchdog_hw));
    memset(&test_chip_reset_hw, 0, sizeof(test_chip_reset_hw));
    normal_boot = true;
    loaded_recovery = false;
    reboot_calls = 0u;
}
void tearDown(void) {}

static void assert_chip_cause(uint32_t flags, system_reset_cause_t expected)
{
    watchdog_hw->scratch[1] = 0u;
    test_chip_reset_hw.chip_reset = flags;
    system_watchdog_enable();
    TEST_ASSERT_EQUAL(expected, system_reset_cause());
    TEST_ASSERT_EQUAL_UINT32(flags, test_chip_reset_hw.chip_reset);
    /* A later subsystem restart must not reuse these read-only flags. */
    system_watchdog_enable();
    TEST_ASSERT_EQUAL(SYSTEM_RESET_CAUSE_UNKNOWN, system_reset_cause());
}

static void test_chip_causes_and_ambiguity(void)
{
    assert_chip_cause(0u, SYSTEM_RESET_CAUSE_UNKNOWN);
#if PICO_RP2350
    assert_chip_cause(POWMAN_CHIP_RESET_HAD_POR_BITS, SYSTEM_RESET_CAUSE_POWER_ON);
    assert_chip_cause(POWMAN_CHIP_RESET_HAD_BOR_BITS, SYSTEM_RESET_CAUSE_BROWNOUT);
    assert_chip_cause(POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS, SYSTEM_RESET_CAUSE_RUN_PIN);
    assert_chip_cause(POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS, SYSTEM_RESET_CAUSE_DEBUGGER);
    assert_chip_cause(POWMAN_CHIP_RESET_HAD_RESCUE_BITS, SYSTEM_RESET_CAUSE_DEBUGGER);
    assert_chip_cause(POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS, SYSTEM_RESET_CAUSE_DEBUGGER);
    const uint32_t watchdog_flags[] = {
        POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS, POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS,
        POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS, POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS,
    };
    for (unsigned index = 0; index < sizeof(watchdog_flags) / sizeof(watchdog_flags[0]); ++index) {
        assert_chip_cause(watchdog_flags[index], SYSTEM_RESET_CAUSE_WATCHDOG);
    }
    assert_chip_cause(POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS, SYSTEM_RESET_CAUSE_UNKNOWN);
    assert_chip_cause(POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS, SYSTEM_RESET_CAUSE_UNKNOWN);
    assert_chip_cause(POWMAN_CHIP_RESET_HAD_POR_BITS | POWMAN_CHIP_RESET_HAD_BOR_BITS, SYSTEM_RESET_CAUSE_UNKNOWN);
    assert_chip_cause(POWMAN_CHIP_RESET_RESCUE_FLAG_BITS | POWMAN_CHIP_RESET_DOUBLE_TAP_BITS,
                      SYSTEM_RESET_CAUSE_UNKNOWN);
#else
    assert_chip_cause(VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS, SYSTEM_RESET_CAUSE_POWER_ON_OR_BROWNOUT);
    assert_chip_cause(VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS, SYSTEM_RESET_CAUSE_RUN_PIN);
    assert_chip_cause(VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS, SYSTEM_RESET_CAUSE_DEBUGGER);
    assert_chip_cause(VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS |
                      VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS, SYSTEM_RESET_CAUSE_UNKNOWN);
    assert_chip_cause(VREG_AND_CHIP_RESET_CHIP_RESET_PSM_RESTART_FLAG_BITS, SYSTEM_RESET_CAUSE_UNKNOWN);
#endif
}

static void test_watchdog_priority_and_consumption(void)
{
    test_chip_reset_hw.chip_reset = UINT32_MAX;
    system_note_hardfault();
    watchdog_hw->scratch[1] = SYSTEM_USER_REBOOT_MARKER;
    watchdog_hw->scratch[4] = 0x6ab73121u;
    watchdog_hw->reason = WATCHDOG_REASON_TIMER_BITS;
    system_watchdog_enable();
    TEST_ASSERT_EQUAL(SYSTEM_RESET_CAUSE_HARDFAULT, system_reset_cause());
    TEST_ASSERT_EQUAL_UINT32(0u, watchdog_hw->scratch[0]);
    TEST_ASSERT_EQUAL_UINT32(SYSTEM_STARTED_MARKER, watchdog_hw->scratch[1]);
    system_init_clock();
    TEST_ASSERT_TRUE(loaded_recovery);
    system_watchdog_enable();
    TEST_ASSERT_EQUAL(SYSTEM_RESET_CAUSE_WATCHDOG, system_reset_cause());
    watchdog_hw->scratch[1] = SYSTEM_CONFIG_SAVE_MARKER;
    system_watchdog_enable();
    TEST_ASSERT_EQUAL(SYSTEM_RESET_CAUSE_WATCHDOG, system_reset_cause());
}

static void test_intentional_paths_and_ownership(void)
{
    const system_reset_cause_t requests[] = {
        SYSTEM_RESET_CAUSE_USER_REBOOT, SYSTEM_RESET_CAUSE_CONFIG_SAVE, SYSTEM_RESET_CAUSE_UNKNOWN,
    };
    for (unsigned index = 0; index < sizeof(requests) / sizeof(requests[0]); ++index) {
        for (unsigned word = 2; word < 8; ++word) watchdog_hw->scratch[word] = word;
        system_reset(requests[index]);
        TEST_ASSERT_EQUAL_UINT(index + 1u, reboot_calls);
        for (unsigned word = 2; word < 8; ++word) {
            TEST_ASSERT_EQUAL_UINT32(word == 4 ? 0u : word, watchdog_hw->scratch[word]);
        }
        system_watchdog_enable();
        TEST_ASSERT_EQUAL(index < 2 ? requests[index] : SYSTEM_RESET_CAUSE_SOFTWARE, system_reset_cause());
        system_init_clock();
        TEST_ASSERT_FALSE(loaded_recovery);
        system_watchdog_enable();
        TEST_ASSERT_EQUAL(SYSTEM_RESET_CAUSE_SOFTWARE, system_reset_cause());
    }
}

static void test_unscoped_markers_and_invalid_boot(void)
{
    const uint32_t markers[] = {SYSTEM_USER_REBOOT_MARKER, SYSTEM_CONFIG_SAVE_MARKER};
    for (unsigned index = 0; index < sizeof(markers) / sizeof(markers[0]); ++index) {
        system_note_hardfault();
        watchdog_hw->scratch[1] = markers[index];
#if PICO_RP2350
        test_chip_reset_hw.chip_reset = POWMAN_CHIP_RESET_HAD_POR_BITS;
#else
        test_chip_reset_hw.chip_reset = VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS;
#endif
        system_watchdog_enable();
        TEST_ASSERT_EQUAL(SYSTEM_RESET_CAUSE_UNKNOWN, system_reset_cause());
        TEST_ASSERT_EQUAL_UINT32(0u, watchdog_hw->scratch[0]);
        TEST_ASSERT_EQUAL_UINT32(SYSTEM_STARTED_MARKER, watchdog_hw->scratch[1]);
    }
    watchdog_hw->reason = WATCHDOG_REASON_FORCE_BITS;
    watchdog_hw->scratch[1] = 123u;
    system_watchdog_enable();
    TEST_ASSERT_EQUAL(SYSTEM_RESET_CAUSE_SOFTWARE, system_reset_cause());
    watchdog_hw->reason = WATCHDOG_REASON_FORCE_BITS | WATCHDOG_REASON_TIMER_BITS;
    watchdog_hw->scratch[1] = SYSTEM_USER_REBOOT_MARKER;
    system_watchdog_enable();
    TEST_ASSERT_EQUAL(SYSTEM_RESET_CAUSE_WATCHDOG, system_reset_cause());
#if PICO_RP2350
    normal_boot = false;
    watchdog_hw->scratch[1] = SYSTEM_USER_REBOOT_MARKER;
    system_watchdog_enable();
    TEST_ASSERT_EQUAL(SYSTEM_RESET_CAUSE_UNKNOWN, system_reset_cause());
#endif
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_chip_causes_and_ambiguity);
    RUN_TEST(test_watchdog_priority_and_consumption);
    RUN_TEST(test_intentional_paths_and_ownership);
    RUN_TEST(test_unscoped_markers_and_invalid_boot);
    return UNITY_END();
}
