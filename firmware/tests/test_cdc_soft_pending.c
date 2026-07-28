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

void test_worker_completion_keeps_newer_control_pending_owner(void)
{
    TEST_ASSERT_FALSE(uart_control_pending_should_clear(true, false));
    TEST_ASSERT_FALSE(uart_control_pending_should_clear(false, true));
    TEST_ASSERT_TRUE(uart_control_pending_should_clear(false, false));
}

void test_rejected_follow_up_invalidates_prior_soft_pending_completion(void)
{
    uint32_t soft_pending_generation = 7u;
    uint32_t generation = soft_pending_generation;
    uint8_t status = 0u;

    uart_control_apply_reject_error(&generation,
                                    &status,
                                    TEST_CONTROL_ERROR_BIT);

    TEST_ASSERT_EQUAL_UINT32(8u, generation);
    TEST_ASSERT_EQUAL_UINT32(TEST_CONTROL_ERROR_BIT, status);
    TEST_ASSERT_FALSE(uart_control_completion_is_current(soft_pending_generation, generation));

    uart_control_apply_completion_error(&status,
                                        TEST_CONTROL_ERROR_BIT,
                                        soft_pending_generation,
                                        generation,
                                        true);
    TEST_ASSERT_EQUAL_UINT8(TEST_CONTROL_ERROR_BIT, status);

    uart_control_apply_completion_error(&status,
                                        TEST_CONTROL_ERROR_BIT,
                                        generation,
                                        generation,
                                        true);
    TEST_ASSERT_EQUAL_UINT8(0u, status);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_deadline_set_only_on_first_arm);
    RUN_TEST(test_worker_completion_keeps_newer_control_pending_owner);
    RUN_TEST(test_rejected_follow_up_invalidates_prior_soft_pending_completion);
    return UNITY_END();
}
