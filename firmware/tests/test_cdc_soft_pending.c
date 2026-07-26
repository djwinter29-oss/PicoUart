/**
 * @file test_cdc_soft_pending.c
 * @brief Unity tests for CDC soft-pending deadline and CONTROL_PENDING ownership helpers.
 */

#include "unity.h"
#include "uart/control_pending.h"
#include "usb/cdc_soft_pending.h"

/** @brief Stand-in for UART_DRIVER_PORT_STATUS_CONTROL_ERROR in host tests. */
#define TEST_CONTROL_ERROR_BIT 0x4u

void setUp(void)
{
}

void tearDown(void)
{
}

void test_deadline_set_only_on_first_arm(void)
{
    TEST_ASSERT_TRUE(usb_cdc_soft_pending_should_set_deadline(false));
    TEST_ASSERT_FALSE(usb_cdc_soft_pending_should_set_deadline(true));
}

void test_preserve_prior_soft_pending_on_reject(void)
{
    TEST_ASSERT_TRUE(usb_cdc_soft_pending_preserve_on_reject(true));
    TEST_ASSERT_FALSE(usb_cdc_soft_pending_preserve_on_reject(false));
}

void test_reject_bump_generation_matches_preserve_policy(void)
{
    /* Preserve path → note_control_error (no bump). */
    TEST_ASSERT_FALSE(usb_cdc_reject_should_bump_generation(true));
    /* Hard reject → report_control_error (bump). */
    TEST_ASSERT_TRUE(usb_cdc_reject_should_bump_generation(false));
}

void test_worker_completion_keeps_newer_control_pending_owner(void)
{
    TEST_ASSERT_FALSE(uart_control_pending_should_clear(true, false));
    TEST_ASSERT_FALSE(uart_control_pending_should_clear(false, true));
    TEST_ASSERT_TRUE(uart_control_pending_should_clear(false, false));
}

void test_soft_pending_completion_uses_original_request_generation(void)
{
    uint32_t soft_pending_generation = 7u;
    uint32_t bumped_after_hard_reject = 8u;

    TEST_ASSERT_TRUE(uart_control_completion_is_current(soft_pending_generation,
                                                         soft_pending_generation));
    TEST_ASSERT_FALSE(uart_control_completion_is_current(soft_pending_generation,
                                                          bumped_after_hard_reject));
}

void test_note_control_error_sets_flag_without_bumping_generation(void)
{
    uint32_t generation = 7u;
    uint32_t status = 0u;

    uart_control_apply_reject_error(&generation,
                                    &status,
                                    TEST_CONTROL_ERROR_BIT,
                                    usb_cdc_reject_should_bump_generation(true));

    TEST_ASSERT_EQUAL_UINT32(7u, generation);
    TEST_ASSERT_EQUAL_UINT32(TEST_CONTROL_ERROR_BIT, status);
    TEST_ASSERT_TRUE(uart_control_completion_is_current(7u, generation));
}

void test_report_control_error_bumps_generation_and_sets_flag(void)
{
    uint32_t generation = 7u;
    uint32_t status = 0u;

    uart_control_apply_reject_error(&generation,
                                    &status,
                                    TEST_CONTROL_ERROR_BIT,
                                    usb_cdc_reject_should_bump_generation(false));

    TEST_ASSERT_EQUAL_UINT32(8u, generation);
    TEST_ASSERT_EQUAL_UINT32(TEST_CONTROL_ERROR_BIT, status);
    TEST_ASSERT_FALSE(uart_control_completion_is_current(7u, generation));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_deadline_set_only_on_first_arm);
    RUN_TEST(test_preserve_prior_soft_pending_on_reject);
    RUN_TEST(test_reject_bump_generation_matches_preserve_policy);
    RUN_TEST(test_worker_completion_keeps_newer_control_pending_owner);
    RUN_TEST(test_soft_pending_completion_uses_original_request_generation);
    RUN_TEST(test_note_control_error_sets_flag_without_bumping_generation);
    RUN_TEST(test_report_control_error_bumps_generation_and_sets_flag);
    return UNITY_END();
}
