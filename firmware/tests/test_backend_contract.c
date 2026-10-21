/**
 * @file test_backend_contract.c
 * @brief Host tests for the common UART backend operation contract.
 */

#include "unity.h"

#include "uart/backend/backend.h"

static bool test_load_config(void *instance, const uart_driver_port_info_t *port_info)
{
    (void)instance;
    return port_info != NULL;
}

static bool test_initialized(const void *instance)
{
    (void)instance;
    return false;
}

static bool test_init(void *instance)
{
    (void)instance;
    return false;
}

static void test_deinit(void *instance)
{
    (void)instance;
}

static void test_poll(void *instance, bool allowed)
{
    (void)instance;
    (void)allowed;
}

static ring_buffer_t *test_ring(void *instance)
{
    static ring_buffer_t ring;
    (void)instance;
    return &ring;
}

static bool test_line_coding_matches(const void *instance,
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

static bool test_set_line_coding(void *instance, const uart_driver_line_coding_t *line_coding)
{
    (void)instance;
    return line_coding != NULL;
}

static bool test_snapshot(const void *instance, uint32_t sequence)
{
    (void)instance;
    (void)sequence;
    return true;
}

static void test_baseline(void *instance)
{
    (void)instance;
}

static uint32_t test_baud_rate(const void *instance)
{
    (void)instance;
    return 115200u;
}

static uart_backend_stats_t test_stats(const void *instance)
{
    (void)instance;
    return (uart_backend_stats_t){0};
}

const uart_backend_ops_t uart_backend_hw_ops = {
    .load_config = test_load_config,
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

const uart_backend_ops_t uart_backend_pio_ops = {0};

void setUp(void)
{
}

void tearDown(void)
{
}

void test_complete_operation_table_is_accepted(void)
{
    TEST_ASSERT_TRUE(uart_backend_ops_is_complete(&uart_backend_hw_ops));
}

void test_incomplete_operation_table_is_rejected(void)
{
    uart_backend_ops_t incomplete_ops = uart_backend_hw_ops;

    incomplete_ops.stats = NULL;
    TEST_ASSERT_FALSE(uart_backend_ops_is_complete(&incomplete_ops));
    incomplete_ops = uart_backend_hw_ops;
    incomplete_ops.load_config = NULL;
    TEST_ASSERT_FALSE(uart_backend_ops_is_complete(&incomplete_ops));
    TEST_ASSERT_FALSE(uart_backend_ops_is_complete(NULL));
}

void test_selection_returns_only_complete_driver_tables(void)
{
    TEST_ASSERT_EQUAL_PTR(&uart_backend_hw_ops, uart_backend_ops_for_type(UART_DRIVER_BACKEND_HW));
    TEST_ASSERT_NULL(uart_backend_ops_for_type(UART_DRIVER_BACKEND_PIO));
    TEST_ASSERT_NULL(uart_backend_ops_for_type((uart_driver_backend_t)UINT32_MAX));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_complete_operation_table_is_accepted);
    RUN_TEST(test_incomplete_operation_table_is_rejected);
    RUN_TEST(test_selection_returns_only_complete_driver_tables);
    return UNITY_END();
}