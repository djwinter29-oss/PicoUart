/**
 * @file test_dma_progress.c
 * @brief Unity tests for RX DMA progress wrap, COUNT masking, and pause-settle policy.
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

void test_pause_settle_policy_constants(void)
{
    TEST_ASSERT_EQUAL_UINT32(5u, UART_DMA_RX_PAUSE_SETTLE_FLOOR_US);
    TEST_ASSERT_EQUAL_UINT32(50u, UART_DMA_RX_PAUSE_SETTLE_TIMEOUT_US);
    TEST_ASSERT_EQUAL_UINT32(2u, UART_DMA_RX_PAUSE_SETTLE_GRACE_US);
    TEST_ASSERT_EQUAL_UINT32(3u, UART_DMA_RX_PAUSE_STABLE_SAMPLES);
}

void test_paused_progress_sample_requires_consecutive_matches(void)
{
    uint32_t last = 100u;
    uint32_t stable = 0u;

    TEST_ASSERT_FALSE(uart_dma_rx_paused_progress_sample(99u, &last, &stable, 3u));
    TEST_ASSERT_EQUAL_UINT32(99u, last);
    TEST_ASSERT_EQUAL_UINT32(0u, stable);

    TEST_ASSERT_FALSE(uart_dma_rx_paused_progress_sample(99u, &last, &stable, 3u));
    TEST_ASSERT_EQUAL_UINT32(1u, stable);
    TEST_ASSERT_FALSE(uart_dma_rx_paused_progress_sample(99u, &last, &stable, 3u));
    TEST_ASSERT_EQUAL_UINT32(2u, stable);
    TEST_ASSERT_TRUE(uart_dma_rx_paused_progress_sample(99u, &last, &stable, 3u));
    TEST_ASSERT_EQUAL_UINT32(3u, stable);
}

void test_paused_progress_sample_resets_streak_on_change(void)
{
    uint32_t last = 50u;
    uint32_t stable = 2u;

    TEST_ASSERT_FALSE(uart_dma_rx_paused_progress_sample(49u, &last, &stable, 3u));
    TEST_ASSERT_EQUAL_UINT32(49u, last);
    TEST_ASSERT_EQUAL_UINT32(0u, stable);
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
    RUN_TEST(test_pause_settle_policy_constants);
    RUN_TEST(test_paused_progress_sample_requires_consecutive_matches);
    RUN_TEST(test_paused_progress_sample_resets_streak_on_change);
    return UNITY_END();
}
