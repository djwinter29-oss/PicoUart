/**
 * @file test_hw_baud_rate.c
 * @brief Unity tests for PL011 baud-rate representability policy.
 */

#include "unity.h"
#include "uart/hw/baud_rate.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_standard_rates_are_accurate_at_125mhz(void)
{
    uint32_t actual_rate;

    TEST_ASSERT_TRUE(hw_uart_baud_rate_supported(115200u, 125000000u, &actual_rate));
    TEST_ASSERT_EQUAL_UINT32(115207u, actual_rate);
    TEST_ASSERT_TRUE(hw_uart_baud_rate_supported(230400u, 125000000u, &actual_rate));
    TEST_ASSERT_EQUAL_UINT32(230414u, actual_rate);
    TEST_ASSERT_TRUE(hw_uart_baud_rate_supported(1000000u, 125000000u, &actual_rate));
    TEST_ASSERT_EQUAL_UINT32(1000000u, actual_rate);
    TEST_ASSERT_TRUE(hw_uart_baud_rate_supported(3000000u, 125000000u, &actual_rate));
    TEST_ASSERT_EQUAL_UINT32(2994011u, actual_rate);
}

void test_impossible_divisors_are_rejected(void)
{
    uint32_t actual_rate;
    uint32_t error_ppm;

    TEST_ASSERT_FALSE(hw_uart_baud_rate_calculate(0u, 125000000u, &actual_rate, &error_ppm));
    TEST_ASSERT_FALSE(hw_uart_baud_rate_supported(1u, 125000000u, &actual_rate));
    TEST_ASSERT_FALSE(hw_uart_baud_rate_calculate(115200u, 0u, &actual_rate, &error_ppm));
    TEST_ASSERT_FALSE(hw_uart_baud_rate_supported(3000000u, 1000000u, &actual_rate));
}

void test_actual_rate_and_error_are_reported(void)
{
    uint32_t actual_rate;
    uint32_t error_ppm;

    TEST_ASSERT_TRUE(hw_uart_baud_rate_calculate(115200u, 125000000u,
                                                  &actual_rate, &error_ppm));
    TEST_ASSERT_TRUE(actual_rate > 0u);
    TEST_ASSERT_TRUE(error_ppm <= HW_UART_BAUD_RATE_MAX_ERROR_PPM);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_standard_rates_are_accurate_at_125mhz);
    RUN_TEST(test_impossible_divisors_are_rejected);
    RUN_TEST(test_actual_rate_and_error_are_reported);
    return UNITY_END();
}