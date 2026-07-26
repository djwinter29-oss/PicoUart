/**
 * @file test_line_coding.c
 * @brief Unity tests for shared UART line-coding parse and validation helpers.
 */

#include "unity.h"
#include "uart/line_coding.h"

/** @brief Representative RP2040 default system clock used by divider tests. */
#define TEST_SYS_HZ 125000000u

void setUp(void)
{
}

void tearDown(void)
{
}

static uart_driver_line_coding_t make_coding(uint32_t baud,
                                             uint8_t data_bits,
                                             uint8_t stop_bits,
                                             uart_driver_parity_t parity)
{
    uart_driver_line_coding_t coding = {
        .baud_rate = baud,
        .data_bits = data_bits,
        .stop_bits = stop_bits,
        .parity = parity,
    };
    return coding;
}

void test_valid_8n1_accepted(void)
{
    uart_driver_line_coding_t coding = make_coding(115200u, 8u, 1u, UART_DRIVER_PARITY_NONE);

    TEST_ASSERT_TRUE(uart_line_coding_is_valid(&coding));
    TEST_ASSERT_TRUE(uart_line_coding_pio_supported(&coding, TEST_SYS_HZ));
}

void test_hw_parity_and_stop_accepted_but_not_pio(void)
{
    uart_driver_line_coding_t odd = make_coding(9600u, 8u, 1u, UART_DRIVER_PARITY_ODD);
    uart_driver_line_coding_t two_stop = make_coding(9600u, 7u, 2u, UART_DRIVER_PARITY_NONE);

    TEST_ASSERT_TRUE(uart_line_coding_is_valid(&odd));
    TEST_ASSERT_FALSE(uart_line_coding_pio_supported(&odd, TEST_SYS_HZ));
    TEST_ASSERT_TRUE(uart_line_coding_is_valid(&two_stop));
    TEST_ASSERT_FALSE(uart_line_coding_pio_supported(&two_stop, TEST_SYS_HZ));
}

void test_baud_bounds(void)
{
    uart_driver_line_coding_t too_slow = make_coding(UART_LINE_CODING_BAUD_MIN - 1u,
                                                     8u,
                                                     1u,
                                                     UART_DRIVER_PARITY_NONE);
    uart_driver_line_coding_t min_ok = make_coding(UART_LINE_CODING_BAUD_MIN,
                                                   8u,
                                                   1u,
                                                   UART_DRIVER_PARITY_NONE);
    uart_driver_line_coding_t max_ok = make_coding(UART_LINE_CODING_BAUD_MAX,
                                                   8u,
                                                   1u,
                                                   UART_DRIVER_PARITY_NONE);
    uart_driver_line_coding_t too_fast = make_coding(UART_LINE_CODING_BAUD_MAX + 1u,
                                                     8u,
                                                     1u,
                                                     UART_DRIVER_PARITY_NONE);

    TEST_ASSERT_FALSE(uart_line_coding_is_valid(&too_slow));
    TEST_ASSERT_TRUE(uart_line_coding_is_valid(&min_ok));
    TEST_ASSERT_TRUE(uart_line_coding_is_valid(&max_ok));
    TEST_ASSERT_FALSE(uart_line_coding_is_valid(&too_fast));
    TEST_ASSERT_FALSE(uart_line_coding_is_valid(NULL));
}

void test_pio_baud_feasibility(void)
{
    uart_driver_line_coding_t low = make_coding(50u, 8u, 1u, UART_DRIVER_PARITY_NONE);
    uart_driver_line_coding_t ok = make_coding(115200u, 8u, 1u, UART_DRIVER_PARITY_NONE);

    /* At 125 MHz, divider for 50 baud is 125e6/(8*50) = 312500 >= 65536. */
    TEST_ASSERT_FALSE(uart_line_coding_pio_baud_feasible(50u, TEST_SYS_HZ));
    TEST_ASSERT_FALSE(uart_line_coding_pio_supported(&low, TEST_SYS_HZ));
    TEST_ASSERT_TRUE(uart_line_coding_pio_baud_feasible(115200u, TEST_SYS_HZ));
    TEST_ASSERT_TRUE(uart_line_coding_pio_supported(&ok, TEST_SYS_HZ));
    TEST_ASSERT_FALSE(uart_line_coding_pio_baud_feasible(115200u, 0u));
}

void test_usb_parse_table(void)
{
    uart_driver_line_coding_t coding;

    TEST_ASSERT_TRUE(uart_line_coding_from_usb(115200u, 0u, 0u, 8u, &coding));
    TEST_ASSERT_EQUAL_UINT32(115200u, coding.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(8u, coding.data_bits);
    TEST_ASSERT_EQUAL_UINT8(1u, coding.stop_bits);
    TEST_ASSERT_EQUAL_INT(UART_DRIVER_PARITY_NONE, coding.parity);

    TEST_ASSERT_TRUE(uart_line_coding_from_usb(57600u, 2u, 1u, 7u, &coding));
    TEST_ASSERT_EQUAL_UINT8(2u, coding.stop_bits);
    TEST_ASSERT_EQUAL_INT(UART_DRIVER_PARITY_ODD, coding.parity);

    TEST_ASSERT_TRUE(uart_line_coding_from_usb(19200u, 0u, 2u, 8u, &coding));
    TEST_ASSERT_EQUAL_INT(UART_DRIVER_PARITY_EVEN, coding.parity);

    TEST_ASSERT_TRUE(uart_line_coding_from_usb(115200u, 0u, 0u, 5u, &coding));
    TEST_ASSERT_EQUAL_UINT8(5u, coding.data_bits);

    /* USB 1.5 stop bits */
    TEST_ASSERT_FALSE(uart_line_coding_from_usb(115200u, 1u, 0u, 8u, &coding));
    /* mark/space parity */
    TEST_ASSERT_FALSE(uart_line_coding_from_usb(115200u, 0u, 3u, 8u, &coding));
    /* zero baud */
    TEST_ASSERT_FALSE(uart_line_coding_from_usb(0u, 0u, 0u, 8u, &coding));
    /* out-of-range data bits */
    TEST_ASSERT_FALSE(uart_line_coding_from_usb(115200u, 0u, 0u, 9u, &coding));
    TEST_ASSERT_FALSE(uart_line_coding_from_usb(115200u, 0u, 0u, 4u, &coding));
    TEST_ASSERT_FALSE(uart_line_coding_from_usb(115200u, 0u, 0u, 8u, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_8n1_accepted);
    RUN_TEST(test_hw_parity_and_stop_accepted_but_not_pio);
    RUN_TEST(test_baud_bounds);
    RUN_TEST(test_pio_baud_feasibility);
    RUN_TEST(test_usb_parse_table);
    return UNITY_END();
}
