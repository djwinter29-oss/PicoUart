/**
 * @file test_backend_policy.c
 * @brief Unity tests for UART backend idle, RX DMA re-arm, and PIO TX policy.
 */

#include "unity.h"
#include "uart/backend_policy.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_hw_line_format_idle_requires_all_clear(void)
{
    TEST_ASSERT_TRUE(uart_hw_line_format_idle(false, false, false, false));
    TEST_ASSERT_FALSE(uart_hw_line_format_idle(true, false, false, false));
    TEST_ASSERT_FALSE(uart_hw_line_format_idle(false, true, false, false));
    TEST_ASSERT_FALSE(uart_hw_line_format_idle(false, false, true, false));
    TEST_ASSERT_FALSE(uart_hw_line_format_idle(false, false, false, true));
}

void test_pio_baud_change_idle_requires_all_clear(void)
{
    TEST_ASSERT_TRUE(uart_pio_baud_change_idle(false, false, true, true, true, true));
    TEST_ASSERT_FALSE(uart_pio_baud_change_idle(true, false, true, true, true, true));
    TEST_ASSERT_FALSE(uart_pio_baud_change_idle(false, true, true, true, true, true));
    TEST_ASSERT_FALSE(uart_pio_baud_change_idle(false, false, false, true, true, true));
    TEST_ASSERT_FALSE(uart_pio_baud_change_idle(false, false, true, false, true, true));
    TEST_ASSERT_FALSE(uart_pio_baud_change_idle(false, false, true, true, false, true));
    TEST_ASSERT_FALSE(uart_pio_baud_change_idle(false, false, true, true, true, false));
}

void test_rx_dma_poll_rearm_only_when_countdown_exhausted(void)
{
    TEST_ASSERT_TRUE(uart_rx_dma_poll_should_rearm(true, false, 0u));
    TEST_ASSERT_FALSE(uart_rx_dma_poll_should_rearm(false, false, 0u));
    TEST_ASSERT_FALSE(uart_rx_dma_poll_should_rearm(true, true, 0u));
    TEST_ASSERT_FALSE(uart_rx_dma_poll_should_rearm(true, false, 1u));
}

void test_pio_tx_action_prefers_dma_above_threshold(void)
{
    TEST_ASSERT_EQUAL_INT(UART_PIO_TX_KEEP_DMA, uart_pio_tx_action(true, 128u, 64u));
    TEST_ASSERT_EQUAL_INT(UART_PIO_TX_START_DMA, uart_pio_tx_action(false, 64u, 64u));
    TEST_ASSERT_EQUAL_INT(UART_PIO_TX_START_DMA, uart_pio_tx_action(false, 256u, 64u));
    TEST_ASSERT_EQUAL_INT(UART_PIO_TX_DRAIN_FIFO, uart_pio_tx_action(false, 63u, 64u));
    TEST_ASSERT_EQUAL_INT(UART_PIO_TX_DRAIN_FIFO, uart_pio_tx_action(false, 0u, 64u));
}

void test_pio_tx_dma_transfer_bytes_clamps_to_occupancy(void)
{
    TEST_ASSERT_EQUAL_UINT(128u, uart_pio_tx_dma_transfer_bytes(128u, 256u));
    TEST_ASSERT_EQUAL_UINT(256u, uart_pio_tx_dma_transfer_bytes(512u, 256u));
    TEST_ASSERT_EQUAL_UINT(0u, uart_pio_tx_dma_transfer_bytes(0u, 256u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hw_line_format_idle_requires_all_clear);
    RUN_TEST(test_pio_baud_change_idle_requires_all_clear);
    RUN_TEST(test_rx_dma_poll_rearm_only_when_countdown_exhausted);
    RUN_TEST(test_pio_tx_action_prefers_dma_above_threshold);
    RUN_TEST(test_pio_tx_dma_transfer_bytes_clamps_to_occupancy);
    return UNITY_END();
}
