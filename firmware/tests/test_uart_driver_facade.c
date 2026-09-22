/**
 * @file test_uart_driver_facade.c
 * @brief Host contract tests for the public UART driver facade.
 */

#include "unity.h"

#include "board/uart_board.h"
#include "pico/time.h"
#include "uart/backend/adapter.h"
#include "uart/control/plane.h"
#include "uart/port_api.h"
#include "uart/uart_driver.h"
#include "uart/worker.h"

absolute_time_t pico_test_time_us;
static uint32_t test_stats_call_count;

const uart_board_port_config_t uart_board_ports[UART_PORT_COUNT] = {0};

const uart_backend_ops_t *uart_backend_ops_for_type(uart_driver_backend_t backend)
{
    (void)backend;
    return NULL;
}

void uart_worker_run(const uart_worker_hooks_t *hooks)
{
    (void)hooks;
}

void uart_backend_enable_rx_dma_irq(void)
{
}

void uart_control_plane_service(uart_control_plane_t *control_plane)
{
    (void)control_plane;
}

bool uart_control_plane_tx_launch_allowed(const uart_control_plane_t *control_plane,
                                          uart_port_id_t port_id)
{
    (void)control_plane;
    (void)port_id;
    return false;
}

static bool test_backend_is_initialized(const uart_backend_instance_t *instance)
{
    (void)instance;
    return false;
}

static ring_buffer_t *test_backend_ring(uart_backend_instance_t *instance)
{
    return (ring_buffer_t *)instance;
}

static uart_backend_stats_t test_backend_stats(const uart_backend_instance_t *instance)
{
    (void)instance;
    test_stats_call_count += 1u;
    return (uart_backend_stats_t){0};
}

static const uart_backend_ops_t test_uninitialized_backend_ops = {
    .is_initialized = test_backend_is_initialized,
    .rx_ring = test_backend_ring,
    .tx_ring = test_backend_ring,
    .stats = test_backend_stats,
};

void setUp(void)
{
    test_stats_call_count = 0u;
}

void tearDown(void)
{
}

void test_uart_driver_exposes_the_six_port_contract(void)
{
    TEST_ASSERT_EQUAL_UINT(6u, uart_driver_port_count());
}

void test_uart_driver_rejects_invalid_port_operations(void)
{
    uart_driver_port_info_t info;
    uart_driver_port_stats_t stats;
    uint8_t byte = 0u;

    TEST_ASSERT_FALSE(uart_driver_port_is_ready(UART_PORT_COUNT));
    TEST_ASSERT_TRUE(uart_driver_port_tx_is_blocked(UART_PORT_COUNT));
    TEST_ASSERT_EQUAL_UINT8(0u, uart_driver_port_status(UART_PORT_COUNT));
    TEST_ASSERT_FALSE(uart_driver_port_info(UART_PORT_COUNT, &info));
    TEST_ASSERT_FALSE(uart_driver_port_stats(UART_PORT_COUNT, &stats));
    TEST_ASSERT_FALSE(uart_driver_line_coding_acceptable(UART_PORT_COUNT, NULL));
    TEST_ASSERT_EQUAL_UINT(0u, uart_driver_drain_rx(UART_PORT_COUNT, 1u, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT(0u, uart_driver_recover_rx(UART_PORT_COUNT));
    TEST_ASSERT_EQUAL_UINT(0u, uart_driver_fill_tx(UART_PORT_COUNT, 1u, NULL, &byte));
}

void test_uart_driver_rejects_null_public_arguments(void)
{
    uart_driver_port_info_t info;
    uart_driver_port_stats_t stats;

    TEST_ASSERT_FALSE(uart_driver_port_info(UART_PORT_0, NULL));
    TEST_ASSERT_FALSE(uart_driver_port_stats(UART_PORT_0, NULL));
    TEST_ASSERT_FALSE(uart_driver_line_coding_acceptable(UART_PORT_0, NULL));
    TEST_ASSERT_FALSE(uart_driver_queue_line_coding(UART_PORT_0, NULL, 1u));
    TEST_ASSERT_FALSE(uart_driver_port_info(UART_PORT_COUNT, &info));
    TEST_ASSERT_FALSE(uart_driver_port_stats(UART_PORT_COUNT, &stats));
}

void test_uart_driver_handles_valid_ports_before_initialization(void)
{
    TEST_ASSERT_TRUE(uart_driver_port_tx_is_blocked(UART_PORT_0));
    TEST_ASSERT_EQUAL_UINT8(0u, uart_driver_port_status(UART_PORT_0));
    TEST_ASSERT_EQUAL_UINT32(0u, uart_driver_mark_control_pending(UART_PORT_0));

    uart_driver_report_control_error(UART_PORT_0);
    uart_driver_report_soft_pending_error(UART_PORT_0, 1u);
    uart_driver_reset_soft_pending(UART_PORT_0);

    TEST_ASSERT_EQUAL_UINT8(0u, uart_driver_port_status(UART_PORT_0));
}

void test_port_api_rejects_stats_from_uninitialized_backend(void)
{
    uart_runtime_port_t port = {0};
    volatile uint32_t stats_sequence[UART_PORT_COUNT] = {0};
    uart_port_api_t api = {
        .ports = &port,
        .stats_sequence = stats_sequence,
    };
    uart_driver_port_stats_t stats;

    port.ops = &test_uninitialized_backend_ops;

    TEST_ASSERT_FALSE(uart_port_api_stats(&api, UART_PORT_0, &stats));
    TEST_ASSERT_EQUAL_UINT32(0u, test_stats_call_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uart_driver_exposes_the_six_port_contract);
    RUN_TEST(test_uart_driver_rejects_invalid_port_operations);
    RUN_TEST(test_uart_driver_rejects_null_public_arguments);
    RUN_TEST(test_uart_driver_handles_valid_ports_before_initialization);
    RUN_TEST(test_port_api_rejects_stats_from_uninitialized_backend);
    return UNITY_END();
}