/**
 * @file test_txstall_wait.c
 * @brief Unity tests for nonblocking fresh TXSTALL evidence.
 */

#include "unity.h"
#include "uart/pio/txstall_wait.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_unstalled_sample_does_not_arm_probe(void)
{
    bool waiting = false;
    TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_WAIT, pio_uart_txstall_sample(&waiting, false));
    TEST_ASSERT_FALSE(waiting);
}

void test_stale_flag_is_cleared_before_accepting_idle(void)
{
    bool waiting = false;
    TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_CLEAR, pio_uart_txstall_sample(&waiting, true));
    TEST_ASSERT_TRUE(waiting);
    TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_WAIT, pio_uart_txstall_sample(&waiting, false));
    TEST_ASSERT_TRUE(waiting);
    TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_IDLE, pio_uart_txstall_sample(&waiting, true));
    TEST_ASSERT_FALSE(waiting);
}

void test_slow_reassertion_does_not_reclear_or_accept_stale_evidence(void)
{
    bool waiting = false;
    TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_CLEAR, pio_uart_txstall_sample(&waiting, true));
    for (unsigned int sample = 0u; sample < 1024u; ++sample) {
        TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_WAIT, pio_uart_txstall_sample(&waiting, false));
        TEST_ASSERT_TRUE(waiting);
    }
    TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_IDLE, pio_uart_txstall_sample(&waiting, true));
}

void test_new_transmission_invalidates_prior_probe(void)
{
    bool waiting = true;
    waiting = false;
    TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_CLEAR, pio_uart_txstall_sample(&waiting, true));
    TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_WAIT, pio_uart_txstall_sample(&waiting, false));
}

void test_each_successful_probe_requires_new_fresh_evidence(void)
{
    bool waiting = false;
    for (unsigned int probe = 0u; probe < 2u; ++probe) {
        TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_CLEAR, pio_uart_txstall_sample(&waiting, true));
        TEST_ASSERT_EQUAL(PIO_UART_TXSTALL_IDLE, pio_uart_txstall_sample(&waiting, true));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_unstalled_sample_does_not_arm_probe);
    RUN_TEST(test_stale_flag_is_cleared_before_accepting_idle);
    RUN_TEST(test_slow_reassertion_does_not_reclear_or_accept_stale_evidence);
    RUN_TEST(test_new_transmission_invalidates_prior_probe);
    RUN_TEST(test_each_successful_probe_requires_new_fresh_evidence);
    return UNITY_END();
}
