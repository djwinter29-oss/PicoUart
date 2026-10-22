/**
 * @file test_uart_driver_facade.c
 * @brief Host contract tests for the public UART driver facade.
 */

#include "unity.h"

#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "uart/backend/backend.h"
#include "uart/control/plane.h"
#include "uart/port_api.h"
#include "uart/uart_driver.h"
#include "uart/worker.h"

#include <string.h>

absolute_time_t pico_test_time_us;
static uint32_t test_stats_call_count;
static void *test_startup_contexts[UART_PORT_COUNT];
static bool test_startup_initialized[UART_PORT_COUNT];
static size_t test_startup_init_calls;
static size_t test_startup_deinit_calls;
static size_t test_startup_baseline_calls;
static size_t test_multicore_launch_calls;
static size_t test_startup_fail_at;
static ring_buffer_t test_startup_rings[UART_PORT_COUNT];

const uart_driver_port_info_t uart_board_ports[UART_PORT_COUNT] = {
    [UART_PORT_0] = {.id = UART_PORT_0, .backend = UART_DRIVER_BACKEND_HW, .baud_rate = 115200u},
    [UART_PORT_1] = {.id = UART_PORT_1, .backend = UART_DRIVER_BACKEND_HW, .baud_rate = 115200u},
    [UART_PORT_2] = {.id = UART_PORT_2, .backend = UART_DRIVER_BACKEND_PIO, .baud_rate = 115200u},
    [UART_PORT_3] = {.id = UART_PORT_3, .backend = UART_DRIVER_BACKEND_PIO, .baud_rate = 115200u},
    [UART_PORT_4] = {.id = UART_PORT_4, .backend = UART_DRIVER_BACKEND_PIO, .baud_rate = 115200u},
    [UART_PORT_5] = {.id = UART_PORT_5, .backend = UART_DRIVER_BACKEND_PIO, .baud_rate = 115200u},
};

bool flash_safe_execute_core_init(void)
{
    return true;
}

static size_t test_startup_port_index(const void *instance)
{
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        if (test_startup_contexts[index] == instance) {
            return index;
        }
    }
    return UART_PORT_COUNT;
}

static bool test_startup_load_config(void *instance, const uart_driver_port_info_t *port_info)
{
    if ((port_info == NULL) || ((unsigned)port_info->id >= UART_PORT_COUNT) ||
        test_startup_initialized[port_info->id]) {
        return false;
    }
    test_startup_contexts[port_info->id] = instance;
    return true;
}

static bool test_startup_is_initialized(const void *instance)
{
    size_t index = test_startup_port_index(instance);
    return (index < UART_PORT_COUNT) && test_startup_initialized[index];
}

static bool test_startup_init(void *instance)
{
    size_t index = test_startup_port_index(instance);
    if (index >= UART_PORT_COUNT) {
        return false;
    }
    ++test_startup_init_calls;
    if (test_startup_init_calls == test_startup_fail_at) {
        return false;
    }
    test_startup_initialized[index] = true;
    return true;
}

static void test_startup_deinit(void *instance)
{
    size_t index = test_startup_port_index(instance);
    if (index < UART_PORT_COUNT) {
        test_startup_initialized[index] = false;
        ++test_startup_deinit_calls;
    }
}

static void test_startup_poll(void *instance, bool tx_launch_allowed)
{
    (void)instance;
    (void)tx_launch_allowed;
}

static ring_buffer_t *test_startup_ring(void *instance)
{
    size_t index = test_startup_port_index(instance);
    return index < UART_PORT_COUNT ? &test_startup_rings[index] : NULL;
}

static bool test_startup_line_coding_matches(const void *instance, const uart_driver_line_coding_t *line_coding)
{
    return (instance != NULL) && (line_coding != NULL);
}

static bool test_startup_line_coding_acceptable(const uart_driver_line_coding_t *line_coding)
{
    return line_coding != NULL;
}

static bool test_startup_set_line_coding(void *instance, const uart_driver_line_coding_t *line_coding)
{
    return (instance != NULL) && (line_coding != NULL);
}

static bool test_startup_reset_buffers(void *instance)
{
    return instance != NULL;
}

static bool test_startup_rx_snapshot_is_current(const void *instance, uint32_t consumer_sequence)
{
    (void)consumer_sequence;
    return instance != NULL;
}

static void test_startup_clear_rx_error_baseline(void *instance)
{
    if (instance != NULL) {
        ++test_startup_baseline_calls;
    }
}

static uint32_t test_startup_baud_rate(const void *instance)
{
    return instance != NULL ? 115200u : 0u;
}

static uart_backend_stats_t test_startup_stats(const void *instance)
{
    (void)instance;
    return (uart_backend_stats_t){0};
}

const uart_backend_ops_t uart_backend_hw_ops = {
    .load_config = test_startup_load_config,
    .is_initialized = test_startup_is_initialized,
    .init = test_startup_init,
    .deinit = test_startup_deinit,
    .poll = test_startup_poll,
    .rx_ring = test_startup_ring,
    .tx_ring = test_startup_ring,
    .line_coding_matches = test_startup_line_coding_matches,
    .line_coding_acceptable = test_startup_line_coding_acceptable,
    .set_line_coding = test_startup_set_line_coding,
    .reset_buffers = test_startup_reset_buffers,
    .rx_snapshot_is_current = test_startup_rx_snapshot_is_current,
    .clear_rx_error_baseline = test_startup_clear_rx_error_baseline,
    .baud_rate = test_startup_baud_rate,
    .stats = test_startup_stats,
};

const uart_backend_ops_t uart_backend_pio_ops = {
    .load_config = test_startup_load_config,
    .is_initialized = test_startup_is_initialized,
    .init = test_startup_init,
    .deinit = test_startup_deinit,
    .poll = test_startup_poll,
    .rx_ring = test_startup_ring,
    .tx_ring = test_startup_ring,
    .line_coding_matches = test_startup_line_coding_matches,
    .line_coding_acceptable = test_startup_line_coding_acceptable,
    .set_line_coding = test_startup_set_line_coding,
    .reset_buffers = test_startup_reset_buffers,
    .rx_snapshot_is_current = test_startup_rx_snapshot_is_current,
    .clear_rx_error_baseline = test_startup_clear_rx_error_baseline,
    .baud_rate = test_startup_baud_rate,
    .stats = test_startup_stats,
};

static void test_record_multicore_launch(pico_test_core_entry_t entry)
{
    TEST_ASSERT_NOT_NULL(entry);
    ++test_multicore_launch_calls;
}

void uart_worker_run(const uart_worker_hooks_t *hooks)
{
    (void)hooks;
}

void hw_uart_driver_enable_rx_dma_irq(void)
{
}

void pio_uart_driver_enable_rx_dma_irq(void)
{
}

void uart_control_plane_service(uart_control_plane_t *control_plane)
{
    (void)control_plane;
}

bool uart_control_plane_tx_launch_allowed(const uart_control_plane_t *control_plane, uart_port_id_t port_id)
{
    (void)control_plane;
    (void)port_id;
    return false;
}

static bool test_backend_is_initialized(const void *instance)
{
    (void)instance;
    return false;
}

static ring_buffer_t *test_backend_ring(void *instance)
{
    return (ring_buffer_t *)instance;
}

static uart_backend_stats_t test_backend_stats(const void *instance)
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
    test_multicore_launch_hook = NULL;
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

void test_startup_rolls_back_partial_backend_init_and_retries(void)
{
    memset(test_startup_contexts, 0, sizeof(test_startup_contexts));
    memset(test_startup_initialized, 0, sizeof(test_startup_initialized));
    memset(test_startup_rings, 0, sizeof(test_startup_rings));
    test_startup_init_calls = 0u;
    test_startup_deinit_calls = 0u;
    test_startup_baseline_calls = 0u;
    test_multicore_launch_calls = 0u;
    test_startup_fail_at = 3u;
    test_multicore_launch_hook = test_record_multicore_launch;

    TEST_ASSERT_FALSE(uart_driver_init());
    TEST_ASSERT_EQUAL_UINT(UART_PORT_COUNT, test_startup_init_calls);
    TEST_ASSERT_EQUAL_UINT(UART_PORT_COUNT - 1u, test_startup_deinit_calls);
    TEST_ASSERT_EQUAL_UINT(0u, test_startup_baseline_calls);
    TEST_ASSERT_EQUAL_UINT(0u, test_multicore_launch_calls);
    TEST_ASSERT_FALSE(uart_driver_worker_is_running());
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        TEST_ASSERT_FALSE(test_startup_initialized[index]);
        TEST_ASSERT_FALSE(uart_driver_port_is_ready((uart_port_id_t)index));
    }

    test_startup_init_calls = 0u;
    test_startup_deinit_calls = 0u;
    test_startup_baseline_calls = 0u;
    test_startup_fail_at = 0u;
    TEST_ASSERT_TRUE(uart_driver_init());
    TEST_ASSERT_EQUAL_UINT(UART_PORT_COUNT, test_startup_init_calls);
    TEST_ASSERT_EQUAL_UINT(0u, test_startup_deinit_calls);
    TEST_ASSERT_EQUAL_UINT(UART_PORT_COUNT, test_startup_baseline_calls);
    TEST_ASSERT_EQUAL_UINT(1u, test_multicore_launch_calls);
    TEST_ASSERT_TRUE(uart_driver_worker_is_running());
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        TEST_ASSERT_TRUE(test_startup_initialized[index]);
        TEST_ASSERT_TRUE(uart_driver_port_is_ready((uart_port_id_t)index));
    }

    TEST_ASSERT_TRUE(uart_driver_init());
    TEST_ASSERT_EQUAL_UINT(1u, test_multicore_launch_calls);

    TEST_ASSERT_TRUE(uart_driver_request_buffer_reset(UART_PORT_2));
    TEST_ASSERT_TRUE(uart_driver_buffer_reset_pending(UART_PORT_2));
    TEST_ASSERT_FALSE(uart_driver_buffer_reset_ready(UART_PORT_2));
    uart_driver_buffer_reset_usb_quiesced(UART_PORT_2);
    TEST_ASSERT_TRUE(uart_driver_buffer_reset_ready(UART_PORT_2));
    TEST_ASSERT_FALSE(uart_driver_request_buffer_reset(UART_PORT_2));
    TEST_ASSERT_TRUE(uart_driver_buffer_reset_ready(UART_PORT_2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uart_driver_exposes_the_six_port_contract);
    RUN_TEST(test_uart_driver_rejects_invalid_port_operations);
    RUN_TEST(test_uart_driver_rejects_null_public_arguments);
    RUN_TEST(test_uart_driver_handles_valid_ports_before_initialization);
    RUN_TEST(test_port_api_rejects_stats_from_uninitialized_backend);
    RUN_TEST(test_startup_rolls_back_partial_backend_init_and_retries);
    return UNITY_END();
}