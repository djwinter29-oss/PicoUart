/**
 * @file test_cdc_soft_pending.c
 * @brief Unity tests for CDC soft-pending deadline and CONTROL_PENDING ownership helpers.
 */

#include "unity.h"
#include "uart/control_pending.h"
#include "usb/cdc_soft_pending.h"

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

void test_worker_completion_keeps_newer_control_pending_owner(void)
{
    TEST_ASSERT_FALSE(uart_control_pending_should_clear(true, false));
    TEST_ASSERT_FALSE(uart_control_pending_should_clear(false, true));
    TEST_ASSERT_TRUE(uart_control_pending_should_clear(false, false));
}

void test_soft_pending_completion_uses_original_request_generation(void)
{
    /*
     * Preserve-path rejects use note_control_error (no generation bump), so the
     * original soft-pending generation stays current for a later successful apply.
     * A full report_control_error-style bump would invalidate the older request.
     */
    uint32_t soft_pending_generation = 7u;
    uint32_t bumped_after_hard_reject = 8u;

    TEST_ASSERT_TRUE(uart_control_completion_is_current(soft_pending_generation,
                                                         soft_pending_generation));
    TEST_ASSERT_FALSE(uart_control_completion_is_current(soft_pending_generation,
                                                          bumped_after_hard_reject));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_deadline_set_only_on_first_arm);
    RUN_TEST(test_preserve_prior_soft_pending_on_reject);
    RUN_TEST(test_worker_completion_keeps_newer_control_pending_owner);
    RUN_TEST(test_soft_pending_completion_uses_original_request_generation);
    return UNITY_END();
}
