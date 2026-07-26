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
    uint32_t valid_soft_pending_generation = 7u;
    uint32_t newer_rejected_generation = 8u;

    TEST_ASSERT_FALSE(uart_control_completion_is_current(valid_soft_pending_generation,
                                                          newer_rejected_generation));
    TEST_ASSERT_TRUE(uart_control_completion_is_current(newer_rejected_generation,
                                                         newer_rejected_generation));
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
