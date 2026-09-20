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

void test_deadline_retained_only_for_identical_retry(void)
{
    TEST_ASSERT_TRUE(usb_cdc_soft_pending_should_set_deadline(false, false));
    TEST_ASSERT_TRUE(usb_cdc_soft_pending_should_set_deadline(false, true));
    TEST_ASSERT_FALSE(usb_cdc_soft_pending_should_set_deadline(true, true));
    TEST_ASSERT_TRUE(usb_cdc_soft_pending_should_set_deadline(true, false));
}

void test_nil_deadline_is_never_a_timeout(void)
{
    /* A reset cancels a request by nil-ing its deadline; that must never
     * read as "timed out", even if time_reached(nil_time) would say yes. */
    TEST_ASSERT_FALSE(usb_cdc_soft_pending_has_timed_out(true, true));
    TEST_ASSERT_FALSE(usb_cdc_soft_pending_has_timed_out(true, false));
    TEST_ASSERT_TRUE(usb_cdc_soft_pending_has_timed_out(false, true));
    TEST_ASSERT_FALSE(usb_cdc_soft_pending_has_timed_out(false, false));
}

void test_mailbox_acceptance_and_sequence_wrap(void)
{
    TEST_ASSERT_TRUE(uart_control_mailbox_is_empty(4u, 4u));
    TEST_ASSERT_FALSE(uart_control_mailbox_is_empty(5u, 4u));
    TEST_ASSERT_EQUAL_UINT32(5u, uart_control_mailbox_next_sequence(4u));
    TEST_ASSERT_EQUAL_UINT32(0u, uart_control_mailbox_next_sequence(UINT32_MAX));
}

void test_worker_completion_keeps_newer_control_pending_owner(void)
{
    TEST_ASSERT_FALSE(uart_control_pending_should_clear(true, false));
    TEST_ASSERT_FALSE(uart_control_pending_should_clear(false, true));
    TEST_ASSERT_TRUE(uart_control_pending_should_clear(false, false));
}

void test_every_control_owner_blocks_tx_ingress(void)
{
    /* The pure admission rule is shared by uart_driver_fill_tx(). */
    TEST_ASSERT_FALSE(uart_control_tx_should_block(false, false, false));
    TEST_ASSERT_TRUE(uart_control_tx_should_block(true, false, false));
    TEST_ASSERT_TRUE(uart_control_tx_should_block(false, true, false));
    TEST_ASSERT_TRUE(uart_control_tx_should_block(false, false, true));
}

void test_tx_boundary_drains_only_at_the_snapped_sequence(void)
{
    TEST_ASSERT_TRUE(uart_control_tx_boundary_drained(32u, 32u));
    TEST_ASSERT_FALSE(uart_control_tx_boundary_drained(31u, 32u));
    TEST_ASSERT_FALSE(uart_control_tx_boundary_drained(33u, 32u));
}

void test_worker_deadline_retained_only_for_identical_retry(void)
{
    TEST_ASSERT_TRUE(uart_control_worker_should_set_deadline(false, false));
    TEST_ASSERT_TRUE(uart_control_worker_should_set_deadline(false, true));
    TEST_ASSERT_FALSE(uart_control_worker_should_set_deadline(true, true));
    TEST_ASSERT_TRUE(uart_control_worker_should_set_deadline(true, false));
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

void test_control_generation_wraps_without_matching_stale_completion(void)
{
    uint32_t generation = UINT32_MAX;
    uint8_t status = 0u;

    uart_control_apply_reject_error(&generation,
                                    &status,
                                    TEST_CONTROL_ERROR_BIT);

    TEST_ASSERT_EQUAL_UINT32(0u, generation);
    TEST_ASSERT_FALSE(uart_control_completion_is_current(UINT32_MAX, generation));
    TEST_ASSERT_TRUE(uart_control_completion_is_current(0u, generation));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_deadline_retained_only_for_identical_retry);
    RUN_TEST(test_nil_deadline_is_never_a_timeout);
    RUN_TEST(test_mailbox_acceptance_and_sequence_wrap);
    RUN_TEST(test_worker_completion_keeps_newer_control_pending_owner);
    RUN_TEST(test_every_control_owner_blocks_tx_ingress);
    RUN_TEST(test_tx_boundary_drains_only_at_the_snapped_sequence);
    RUN_TEST(test_worker_deadline_retained_only_for_identical_retry);
    RUN_TEST(test_rejected_follow_up_invalidates_prior_soft_pending_completion);
    RUN_TEST(test_control_generation_wraps_without_matching_stale_completion);
    return UNITY_END();
}
