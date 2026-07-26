/**
 * @file test_cdc_soft_pending.c
 * @brief Unity tests for CDC soft-pending deadline and CONTROL_PENDING ownership helpers.
 */

#include "unity.h"
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_deadline_set_only_on_first_arm);
    RUN_TEST(test_preserve_prior_soft_pending_on_reject);
    return UNITY_END();
}
