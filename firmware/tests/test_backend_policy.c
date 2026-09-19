/**
 * @file test_backend_policy.c
 * @brief Unity tests for UART backend idle, DMA IRQ, PIO TX, and worker heartbeat policy.
 */

#include "unity.h"
#include "uart/backend_policy.h"
#include "uart/worker_health.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_hw_line_format_idle_requires_all_clear(void)
{
    TEST_ASSERT_TRUE(uart_hw_line_format_idle(false, false, false, false));
    TEST_ASSERT_TRUE(uart_hw_line_format_idle(true, false, false, false));
    TEST_ASSERT_FALSE(uart_hw_line_format_idle(false, true, false, false));
    TEST_ASSERT_FALSE(uart_hw_line_format_idle(false, false, true, false));
    TEST_ASSERT_FALSE(uart_hw_line_format_idle(false, false, false, true));
}

void test_pio_baud_change_idle_requires_all_clear(void)
{
    TEST_ASSERT_TRUE(uart_pio_baud_change_idle(false, false, true, true, true, true));
    TEST_ASSERT_FALSE(uart_pio_baud_change_idle(true, false, true, true, true, true));
    TEST_ASSERT_TRUE(uart_pio_baud_change_idle(false, true, true, true, true, true));
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

void test_tx_dma_transfer_bytes_clamps_to_occupancy_and_budget(void)
{
    TEST_ASSERT_EQUAL_UINT(128u, uart_tx_transfer_bytes(128u, 256u, 115200u, 10u, 25u));
    TEST_ASSERT_EQUAL_UINT(256u, uart_tx_transfer_bytes(512u, 256u, 115200u, 10u, 25u));
    TEST_ASSERT_EQUAL_UINT(288u, uart_tx_transfer_bytes(512u, 512u, 115200u, 10u, 25u));
    TEST_ASSERT_EQUAL_UINT(240u, uart_tx_transfer_bytes(512u, 512u, 115200u, 12u, 25u));
    TEST_ASSERT_EQUAL_UINT(1u, uart_tx_transfer_bytes(512u, 512u, 300u, 10u, 25u));
    TEST_ASSERT_EQUAL_UINT(0u, uart_tx_transfer_bytes(0u, 256u, 115200u, 10u, 25u));
    TEST_ASSERT_EQUAL_UINT(0u, uart_tx_transfer_bytes(10u, 10u, 0u, 10u, 25u));
}

void test_rts_hysteresis_reflects_occupancy_on_reconfigure(void)
{
    TEST_ASSERT_TRUE(uart_rx_rts_should_assert(false, 0u, 100u));
    TEST_ASSERT_FALSE(uart_rx_rts_should_assert(true, 75u, 100u));
    TEST_ASSERT_FALSE(uart_rx_rts_should_assert(false, 60u, 100u));
    TEST_ASSERT_TRUE(uart_rx_rts_should_assert(true, 60u, 100u));
    TEST_ASSERT_TRUE(uart_rx_rts_should_assert(false, 50u, 100u));
}

void test_dma_irq_services_only_pending_owners(void)
{
    TEST_ASSERT_TRUE(uart_dma_irq_should_service_owner(true, true));
    TEST_ASSERT_FALSE(uart_dma_irq_should_service_owner(false, true));
    TEST_ASSERT_FALSE(uart_dma_irq_should_service_owner(true, false));
    TEST_ASSERT_FALSE(uart_dma_irq_should_service_owner(false, false));
}

void test_dma_irq_rearms_only_an_exhausted_channel(void)
{
    TEST_ASSERT_TRUE(uart_rx_dma_irq_should_rearm(true, true, false, 0u));
    TEST_ASSERT_FALSE(uart_rx_dma_irq_should_rearm(true, true, true, 0u));
    TEST_ASSERT_FALSE(uart_rx_dma_irq_should_rearm(true, true, false, 1u));
    TEST_ASSERT_FALSE(uart_rx_dma_irq_should_rearm(false, true, false, 0u));
    TEST_ASSERT_FALSE(uart_rx_dma_irq_should_rearm(true, false, false, 0u));
}

void test_worker_heartbeat_fresh_on_increment(void)
{
    uint32_t last = 4u;
    uint32_t last_change_ms = 100u;

    TEST_ASSERT_TRUE(uart_worker_heartbeat_is_fresh(5u, &last, 2500u, &last_change_ms,
                                                    UART_WORKER_HEARTBEAT_STALE_MS));
    TEST_ASSERT_EQUAL_UINT32(5u, last);
    TEST_ASSERT_EQUAL_UINT32(2500u, last_change_ms);
}

void test_worker_heartbeat_stale_when_silent(void)
{
    uint32_t last = 9u;
    uint32_t last_change_ms = 1000u;

    TEST_ASSERT_TRUE(uart_worker_heartbeat_is_fresh(9u, &last, 2999u, &last_change_ms,
                                                    UART_WORKER_HEARTBEAT_STALE_MS));
    TEST_ASSERT_FALSE(uart_worker_heartbeat_is_fresh(9u, &last, 3000u, &last_change_ms,
                                                     UART_WORKER_HEARTBEAT_STALE_MS));
    TEST_ASSERT_EQUAL_UINT32(9u, last);
    TEST_ASSERT_EQUAL_UINT32(1000u, last_change_ms);
}

void test_worker_heartbeat_boot_window_is_fresh(void)
{
    uint32_t last = 0u;
    uint32_t last_change_ms = 0u;

    TEST_ASSERT_TRUE(uart_worker_heartbeat_is_fresh(0u, &last, 1999u, &last_change_ms,
                                                    UART_WORKER_HEARTBEAT_STALE_MS));
    TEST_ASSERT_FALSE(uart_worker_heartbeat_is_fresh(0u, &last, 2000u, &last_change_ms,
                                                     UART_WORKER_HEARTBEAT_STALE_MS));
}

void test_worker_heartbeat_handles_timestamp_wrap(void)
{
    uint32_t last = 12u;
    uint32_t last_change_ms = UINT32_MAX - 100u;

    TEST_ASSERT_TRUE(uart_worker_heartbeat_is_fresh(12u, &last, 50u, &last_change_ms,
                                                    200u));
    TEST_ASSERT_FALSE(uart_worker_heartbeat_is_fresh(12u, &last, 150u, &last_change_ms,
                                                     200u));
}

void test_worker_heartbeat_rejects_null_state(void)
{
    uint32_t last = 0u;
    uint32_t last_change_ms = 0u;

    TEST_ASSERT_FALSE(uart_worker_heartbeat_is_fresh(1u, NULL, 0u, &last_change_ms, 2000u));
    TEST_ASSERT_FALSE(uart_worker_heartbeat_is_fresh(1u, &last, 0u, NULL, 2000u));
    TEST_ASSERT_FALSE(uart_worker_heartbeat_is_fresh(1u, &last, 0u, &last_change_ms, 0u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hw_line_format_idle_requires_all_clear);
    RUN_TEST(test_pio_baud_change_idle_requires_all_clear);
    RUN_TEST(test_rts_hysteresis_reflects_occupancy_on_reconfigure);
    RUN_TEST(test_rx_dma_poll_rearm_only_when_countdown_exhausted);
    RUN_TEST(test_pio_tx_action_prefers_dma_above_threshold);
    RUN_TEST(test_tx_dma_transfer_bytes_clamps_to_occupancy_and_budget);
    RUN_TEST(test_dma_irq_services_only_pending_owners);
    RUN_TEST(test_dma_irq_rearms_only_an_exhausted_channel);
    RUN_TEST(test_worker_heartbeat_fresh_on_increment);
    RUN_TEST(test_worker_heartbeat_stale_when_silent);
    RUN_TEST(test_worker_heartbeat_boot_window_is_fresh);
    RUN_TEST(test_worker_heartbeat_handles_timestamp_wrap);
    RUN_TEST(test_worker_heartbeat_rejects_null_state);
    return UNITY_END();
}
