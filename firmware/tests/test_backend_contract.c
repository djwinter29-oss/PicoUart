/**
 * @file test_backend_contract.c
 * @brief Host tests for the common UART backend operation contract.
 */

#include "unity.h"

#include "uart/backend/adapter.h"

static bool test_initialized(const uart_backend_instance_t *instance)
{
    (void)instance;
    return false;
}

static bool test_init(uart_backend_instance_t *instance)
{
    (void)instance;
    return false;
}

static void test_deinit(uart_backend_instance_t *instance)
{
    (void)instance;
}

static void test_poll(uart_backend_instance_t *instance, bool allowed)
{
    (void)instance;
    (void)allowed;
}

static ring_buffer_t *test_ring(uart_backend_instance_t *instance)
{
    return &instance->hw.rx_ring;
}

static bool test_line_coding_matches(const uart_backend_instance_t *instance,
                                     const uart_driver_line_coding_t *line_coding)
{
    (void)instance;
    (void)line_coding;
    return false;
}

static bool test_line_coding_acceptable(const uart_driver_line_coding_t *line_coding)
{
    return line_coding != NULL;
}

static bool test_set_line_coding(uart_backend_instance_t *instance,
                                 const uart_driver_line_coding_t *line_coding)
{
    (void)instance;
    return line_coding != NULL;
}

static bool test_snapshot(const uart_backend_instance_t *instance, uint32_t sequence)
{
    (void)instance;
    (void)sequence;
    return true;
}

static void test_baseline(uart_backend_instance_t *instance)
{
    (void)instance;
}

static uint32_t test_baud_rate(const uart_backend_instance_t *instance)
{
    (void)instance;
    return 115200u;
}

static uart_backend_stats_t test_stats(const uart_backend_instance_t *instance)
{
    (void)instance;
    return (uart_backend_stats_t){0};
}

static const uart_backend_ops_t complete_ops = {
    .is_initialized = test_initialized,
    .init = test_init,
    .deinit = test_deinit,
    .poll = test_poll,
    .rx_ring = test_ring,
    .tx_ring = test_ring,
    .line_coding_matches = test_line_coding_matches,
    .line_coding_acceptable = test_line_coding_acceptable,
    .set_line_coding = test_set_line_coding,
    .rx_snapshot_is_current = test_snapshot,
    .clear_rx_error_baseline = test_baseline,
    .baud_rate = test_baud_rate,
    .stats = test_stats,
};

void setUp(void)
{
}

void tearDown(void)
{
}

void test_complete_operation_table_is_accepted(void)
{
    TEST_ASSERT_TRUE(uart_backend_ops_is_complete(&complete_ops));
}

void test_incomplete_operation_table_is_rejected(void)
{
    uart_backend_ops_t incomplete_ops = complete_ops;

    incomplete_ops.stats = NULL;
    TEST_ASSERT_FALSE(uart_backend_ops_is_complete(&incomplete_ops));
    TEST_ASSERT_FALSE(uart_backend_ops_is_complete(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_complete_operation_table_is_accepted);
    RUN_TEST(test_incomplete_operation_table_is_rejected);
    return UNITY_END();
}