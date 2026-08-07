/**
 * @file test_txstall_wait.c
 * @brief Unity tests for PIO TXSTALL re-assert wait timing.
 */

#include "unity.h"
#include "uart/pio/txstall_wait.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_txstall_wait_floors_high_baud(void)
{
    /* ceil(375000 / 1e6) = 1 → floor 2 */
    TEST_ASSERT_EQUAL_UINT32(PIO_UART_TXSTALL_REASSERT_WAIT_FLOOR_US,
                             pio_uart_txstall_reassert_wait_us(1000000u));
}

void test_txstall_wait_mid_baud(void)
{
    /* ceil(375000 / 115200) = 4 */
    TEST_ASSERT_EQUAL_UINT32(4u, pio_uart_txstall_reassert_wait_us(115200u));
    /* ceil(375000 / 300) = 1250 */
    TEST_ASSERT_EQUAL_UINT32(1250u, pio_uart_txstall_reassert_wait_us(300u));
}

void test_txstall_wait_covers_low_baud_pio_cycles(void)
{
    /* ceil(375000 / 9600) = 40 us > floor */
    TEST_ASSERT_EQUAL_UINT32(40u, pio_uart_txstall_reassert_wait_us(9600u));
    /* ceil(375000 / 1200) = 313 us */
    TEST_ASSERT_EQUAL_UINT32(313u, pio_uart_txstall_reassert_wait_us(1200u));
    /* ceil(375000 / 50) = 7500 us (BAUD_MIN) */
    TEST_ASSERT_EQUAL_UINT32(7500u, pio_uart_txstall_reassert_wait_us(50u));
}

void test_txstall_wait_treats_zero_baud_as_one(void)
{
    TEST_ASSERT_EQUAL_UINT32(375000u, pio_uart_txstall_reassert_wait_us(0u));
}

void test_txstall_wait_cycle_constant(void)
{
    TEST_ASSERT_EQUAL_UINT32(3u, PIO_UART_TXSTALL_REASSERT_PIO_CYCLES);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_txstall_wait_floors_high_baud);
    RUN_TEST(test_txstall_wait_mid_baud);
    RUN_TEST(test_txstall_wait_covers_low_baud_pio_cycles);
    RUN_TEST(test_txstall_wait_treats_zero_baud_as_one);
    RUN_TEST(test_txstall_wait_cycle_constant);
    return UNITY_END();
}
