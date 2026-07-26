/**
 * @file test_dma_progress.c
 * @brief Unity tests for RX DMA progress wrap and RP2040/RP2350 COUNT masking.
 */

#include "unity.h"
#include "uart/dma_progress_math.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_bytes_produced_monotonic(void)
{
    TEST_ASSERT_EQUAL_UINT32(100u,
                             uart_dma_rx_bytes_produced(150u, 50u, UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040));
    TEST_ASSERT_EQUAL_UINT32(0u,
                             uart_dma_rx_bytes_produced(50u, 50u, UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040));
}

void test_bytes_produced_wrap_rp2040(void)
{
    uint32_t max = UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040;
    uint32_t last = max - 10u;
    uint32_t progress = 5u;

    TEST_ASSERT_EQUAL_UINT32(15u, uart_dma_rx_bytes_produced(progress, last, max));
}

void test_bytes_produced_wrap_rp2350(void)
{
    uint32_t max = UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350;
    uint32_t last = max - 3u;
    uint32_t progress = 7u;

    TEST_ASSERT_EQUAL_UINT32(10u, uart_dma_rx_bytes_produced(progress, last, max));
}

void test_mask_remaining_clears_rp2350_mode_bits(void)
{
    uint32_t raw = 0xf0000005u; /* MODE nibble set + COUNT 5 */

    TEST_ASSERT_EQUAL_UINT32(5u,
                             uart_dma_rx_mask_remaining(raw, UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350));
    TEST_ASSERT_EQUAL_UINT32(raw,
                             uart_dma_rx_mask_remaining(raw, UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040));
}

void test_progress_from_remaining(void)
{
    TEST_ASSERT_EQUAL_UINT32(
        10u,
        uart_dma_rx_progress_from_remaining(UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350 - 10u,
                                            UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350));
    TEST_ASSERT_EQUAL_UINT32(
        0u,
        uart_dma_rx_progress_from_remaining(UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040,
                                            UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040));
}

void test_platform_count_constants(void)
{
    TEST_ASSERT_EQUAL_UINT32(0xffffffffu, UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040);
    TEST_ASSERT_EQUAL_UINT32(0x0fffffffu, UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bytes_produced_monotonic);
    RUN_TEST(test_bytes_produced_wrap_rp2040);
    RUN_TEST(test_bytes_produced_wrap_rp2350);
    RUN_TEST(test_mask_remaining_clears_rp2350_mode_bits);
    RUN_TEST(test_progress_from_remaining);
    RUN_TEST(test_platform_count_constants);
    return UNITY_END();
}
