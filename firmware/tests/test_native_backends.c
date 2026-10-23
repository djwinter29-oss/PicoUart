/**
 * @file test_native_backends.c
 * @brief Exercise real backend tables and policy with only physical I/O stubbed.
 */

#include "unity.h"
#include "hardware/clocks.h"
#include "uart/uart_driver.h"
#include "uart/backend/backend.h"
#include "uart/hw/hw_uart_driver.h"
#include "uart/pio/pio_uart_driver_internal.h"

#include <string.h>

static hw_uart_driver_t hardware;
static pio_uart_driver_t pio;
static uint32_t system_hz;
static uint32_t peripheral_hz;
static unsigned int apply_calls;
static void *applied_context;
static uart_driver_line_coding_t applied;
static uart_parity_t applied_sdk_parity;
static bool apply_result;
static unsigned int clock_queries;

uart_inst_t uart0_test_instance;
uart_inst_t uart1_test_instance;
pio_hw_t pio0_test_instance;
pio_hw_t pio1_test_instance;

uint32_t clock_get_hz(unsigned int clock)
{
    clock_queries += 1u;
    TEST_ASSERT_TRUE((clock == clk_sys) || (clock == clk_peri));
    return clock == clk_sys ? system_hz : peripheral_hz;
}

bool hw_uart_driver_apply_line_format(void *context, uint32_t baud_rate, uint8_t data_bits, uint8_t stop_bits,
                                     uart_parity_t parity)
{
    apply_calls += 1u;
    applied_context = context;
    applied.baud_rate = baud_rate;
    applied.data_bits = data_bits;
    applied.stop_bits = stop_bits;
    applied_sdk_parity = parity;
    return apply_result;
}

bool pio_uart_driver_apply_baud_rate(void *context, uint32_t baud_rate)
{
    apply_calls += 1u;
    applied_context = context;
    applied.baud_rate = baud_rate;
    return apply_result;
}

bool hw_uart_driver_start(void *context)
{
    (void)context;
    TEST_FAIL_MESSAGE("unexpected HW initialization");
    return false;
}

bool pio_uart_driver_start(void *context)
{
    (void)context;
    TEST_FAIL_MESSAGE("unexpected PIO initialization");
    return false;
}

void hw_uart_driver_deinit(void *context)
{
    (void)context;
    TEST_FAIL_MESSAGE("unexpected HW deinitialization");
}

void pio_uart_driver_deinit(void *context)
{
    (void)context;
    TEST_FAIL_MESSAGE("unexpected PIO deinitialization");
}

bool hw_uart_driver_restart(void *context)
{
    if (context == NULL) {
        return false;
    }
    TEST_FAIL_MESSAGE("unexpected HW buffer reset");
    return false;
}

bool pio_uart_driver_restart(void *context)
{
    if (context == NULL) {
        return false;
    }
    TEST_FAIL_MESSAGE("unexpected PIO buffer reset");
    return false;
}

void hw_uart_driver_poll(void *context, bool tx_launch_allowed)
{
    (void)context;
    (void)tx_launch_allowed;
    TEST_FAIL_MESSAGE("unexpected HW polling");
}

void pio_uart_driver_poll(void *context, bool tx_launch_allowed)
{
    (void)context;
    (void)tx_launch_allowed;
    TEST_FAIL_MESSAGE("unexpected PIO polling");
}

void hw_uart_driver_clear_rx_error_baseline(void *context)
{
    (void)context;
    TEST_FAIL_MESSAGE("unexpected HW baseline reset");
}

void pio_uart_driver_clear_rx_error_baseline(void *context)
{
    (void)context;
    TEST_FAIL_MESSAGE("unexpected PIO baseline reset");
}

bool hw_uart_driver_rx_snapshot_is_current(const void *context, uint32_t consumer_sequence)
{
    (void)context;
    (void)consumer_sequence;
    TEST_FAIL_MESSAGE("unexpected HW DMA snapshot");
    return false;
}

bool pio_uart_driver_rx_snapshot_is_current(const void *context, uint32_t consumer_sequence)
{
    (void)context;
    (void)consumer_sequence;
    TEST_FAIL_MESSAGE("unexpected PIO DMA snapshot");
    return false;
}

void setUp(void)
{
    memset(&hardware, 0, sizeof(hardware));
    memset(&pio, 0, sizeof(pio));
    system_hz = 125000000u;
    peripheral_hz = 125000000u;
    apply_calls = 0u;
    applied_context = NULL;
    applied = (uart_driver_line_coding_t){0};
    applied_sdk_parity = UART_PARITY_NONE;
    apply_result = true;
    clock_queries = 0u;
}

void tearDown(void)
{
}

void test_real_tables_are_complete_and_selected(void)
{
    TEST_ASSERT_TRUE(uart_backend_ops_is_complete(&uart_backend_hw_ops));
    TEST_ASSERT_TRUE(uart_backend_ops_is_complete(&uart_backend_pio_ops));
    TEST_ASSERT_EQUAL_PTR(&uart_backend_hw_ops, uart_backend_ops_for_type(UART_DRIVER_BACKEND_HW));
    TEST_ASSERT_EQUAL_PTR(&uart_backend_pio_ops, uart_backend_ops_for_type(UART_DRIVER_BACKEND_PIO));
}

void test_pio_reinitialization_preserves_resource_ownership_and_state(void)
{
    TEST_ASSERT_TRUE(uart_backend_pio_ops.load_config(&pio, &uart_board_ports[UART_PORT_2]));
    pio.initialized = true;
    pio.rx_dma_channel = 6;
    pio.tx_dma_channel = 7;
    pio.tx_sm_claimed = true;
    pio.rx_sm_claimed = true;
    pio.tx_dma_active = true;
    pio.tx_dma_bytes_in_flight = 32u;
    pio.rx_dma_last_progress = 45u;
    pio.baud_rate = 230400u;
    pio.tx_polled_bytes = 11u;
    pio.tx_dma_bytes = 23u;
    pio.uart_rx_bytes = 45u;
    pio.rx_error_count = 2u;
    pio.rx_ring.producer = 45u;
    pio.rx_ring.consumer = 12u;
    pio.tx_ring.producer = 64u;
    pio.tx_ring.consumer = 8u;
    pio_uart_driver_t before;
    memcpy(&before, &pio, sizeof(before));

    TEST_ASSERT_FALSE(uart_backend_pio_ops.init(&pio));
    TEST_ASSERT_EQUAL_MEMORY(&before, &pio, sizeof(pio));
    TEST_ASSERT_EQUAL_UINT(0u, clock_queries);
    TEST_ASSERT_EQUAL_UINT(0u, apply_calls);
    TEST_ASSERT_FALSE(uart_backend_pio_ops.init(NULL));
}

void test_hw_reinitialization_preserves_resource_ownership_and_state(void)
{
    TEST_ASSERT_TRUE(uart_backend_hw_ops.load_config(&hardware, &uart_board_ports[UART_PORT_0]));
    hardware.initialized = true;
    hardware.rx_dma_channel = 2;
    hardware.tx_dma_channel = 3;
    hardware.tx_active = true;
    hardware.tx_dma_bytes_in_flight = 64u;
    hardware.rx_dma_last_progress = 17u;
    hardware.baud_rate = 230400u;
    hardware.data_bits = 7u;
    hardware.stop_bits = 2u;
    hardware.parity = UART_PARITY_EVEN;
    hardware.uart_tx_bytes = 32u;
    hardware.uart_rx_bytes = 17u;
    hardware.rx_error_count = 1u;
    hardware.rx_ring.producer = 17u;
    hardware.rx_ring.consumer = 4u;
    hardware.tx_ring.producer = 128u;
    hardware.tx_ring.consumer = 16u;
    hw_uart_driver_t before;
    memcpy(&before, &hardware, sizeof(before));

    TEST_ASSERT_FALSE(uart_backend_hw_ops.init(&hardware));
    TEST_ASSERT_EQUAL_MEMORY(&before, &hardware, sizeof(hardware));
    TEST_ASSERT_FALSE(uart_backend_hw_ops.init(NULL));
    TEST_ASSERT_EQUAL_UINT(0u, clock_queries);
    TEST_ASSERT_EQUAL_UINT(0u, apply_calls);
}

void test_invalid_requests_never_reach_hardware_application(void)
{
    const uart_driver_line_coding_t invalid[] = {
        {115200u, 8u, 1u, (uart_driver_parity_t)3},
        {115200u, 8u, 1u, (uart_driver_parity_t)-1},
        {115200u, 4u, 1u, UART_DRIVER_PARITY_NONE},
        {115200u, 8u, 3u, UART_DRIVER_PARITY_NONE},
        {0u, 8u, 1u, UART_DRIVER_PARITY_NONE},
        {49u, 8u, 1u, UART_DRIVER_PARITY_NONE},
        {3000001u, 8u, 1u, UART_DRIVER_PARITY_NONE},
    };
    hardware.initialized = true;
    pio.initialized = true;
    for (size_t index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        TEST_ASSERT_FALSE(uart_backend_hw_ops.line_coding_acceptable(&invalid[index]));
        TEST_ASSERT_FALSE(uart_backend_hw_ops.line_coding_matches(&hardware, &invalid[index]));
        TEST_ASSERT_FALSE(uart_backend_hw_ops.set_line_coding(&hardware, &invalid[index]));
        TEST_ASSERT_FALSE(uart_backend_pio_ops.line_coding_acceptable(&invalid[index]));
        TEST_ASSERT_FALSE(uart_backend_pio_ops.line_coding_matches(&pio, &invalid[index]));
        TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, &invalid[index]));
    }
    TEST_ASSERT_FALSE(uart_backend_hw_ops.set_line_coding(&hardware, NULL));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, NULL));
    TEST_ASSERT_EQUAL_UINT(0u, apply_calls);
}

void test_hardware_setter_translates_all_supported_parities_and_propagates_failure(void)
{
    const uart_driver_parity_t common[] = {UART_DRIVER_PARITY_NONE, UART_DRIVER_PARITY_ODD, UART_DRIVER_PARITY_EVEN};
    const uart_parity_t sdk[] = {UART_PARITY_NONE, UART_PARITY_ODD, UART_PARITY_EVEN};
    hardware.initialized = true;
    for (size_t index = 0u; index < sizeof(common) / sizeof(common[0]); ++index) {
        uart_driver_line_coding_t request = {115200u, 7u, 2u, common[index]};
        TEST_ASSERT_TRUE(uart_backend_hw_ops.set_line_coding(&hardware, &request));
        TEST_ASSERT_EQUAL_UINT(sdk[index], applied_sdk_parity);
        TEST_ASSERT_EQUAL_UINT32(115200u, applied.baud_rate);
        TEST_ASSERT_EQUAL_UINT8(7u, applied.data_bits);
        TEST_ASSERT_EQUAL_UINT8(2u, applied.stop_bits);
        TEST_ASSERT_EQUAL_PTR(&hardware, applied_context);
    }
    uart_driver_line_coding_t request = {115200u, 8u, 1u, UART_DRIVER_PARITY_NONE};
    apply_result = false;
    TEST_ASSERT_FALSE(uart_backend_hw_ops.set_line_coding(&hardware, &request));
    TEST_ASSERT_EQUAL_UINT(4u, apply_calls);
}

void test_hardware_matching_uses_applied_divider_rate_and_peripheral_clock(void)
{
    uart_driver_line_coding_t request = {115200u, 8u, 1u, UART_DRIVER_PARITY_ODD};
    hardware.initialized = true;
    hardware.baud_rate = 115207u;
    hardware.data_bits = 8u;
    hardware.stop_bits = 1u;
    hardware.parity = UART_PARITY_ODD;
    TEST_ASSERT_TRUE(uart_backend_hw_ops.line_coding_acceptable(&request));
    TEST_ASSERT_TRUE(uart_backend_hw_ops.line_coding_matches(&hardware, &request));
    hardware.baud_rate = request.baud_rate;
    TEST_ASSERT_FALSE(uart_backend_hw_ops.line_coding_matches(&hardware, &request));
    hardware.baud_rate = 115207u;
    request.parity = UART_DRIVER_PARITY_EVEN;
    TEST_ASSERT_FALSE(uart_backend_hw_ops.line_coding_matches(&hardware, &request));
    peripheral_hz = 1000000u;
    TEST_ASSERT_FALSE(uart_backend_hw_ops.line_coding_acceptable(&request));
}

void test_pio_setter_rejects_non_8n1_and_rx_divider_limits(void)
{
    uart_driver_line_coding_t request = {115200u, 8u, 1u, UART_DRIVER_PARITY_NONE};
    pio.initialized = true;
    pio.baud_rate = request.baud_rate;
    TEST_ASSERT_TRUE(uart_backend_pio_ops.line_coding_matches(&pio, &request));
    TEST_ASSERT_TRUE(uart_backend_pio_ops.set_line_coding(&pio, &request));
    TEST_ASSERT_EQUAL_PTR(&pio, applied_context);
    TEST_ASSERT_EQUAL_UINT32(request.baud_rate, applied.baud_rate);
    apply_result = false;
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, &request));
    TEST_ASSERT_EQUAL_UINT(2u, apply_calls);
    apply_calls = 0u;
    request.parity = UART_DRIVER_PARITY_ODD;
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, &request));
    request.parity = UART_DRIVER_PARITY_NONE;
    request.data_bits = 7u;
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, &request));
    request.data_bits = 8u;
    request.stop_bits = 2u;
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, &request));
    request.stop_bits = 1u;
    request.baud_rate = 3000000u;
    system_hz = 24000000u;
    TEST_ASSERT_FALSE(uart_backend_pio_ops.line_coding_acceptable(&request));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, &request));
    TEST_ASSERT_EQUAL_UINT(0u, apply_calls);
}

void test_null_api_inputs_return_neutral_results_without_hardware_access(void)
{
    const uart_backend_ops_t *backends[] = {&uart_backend_hw_ops, &uart_backend_pio_ops};
    uart_driver_line_coding_t request = {115200u, 8u, 1u, UART_DRIVER_PARITY_NONE};
    for (size_t index = 0u; index < sizeof(backends) / sizeof(backends[0]); ++index) {
        const uart_backend_ops_t *ops = backends[index];
        TEST_ASSERT_FALSE(ops->is_initialized(NULL));
        TEST_ASSERT_NULL(ops->rx_ring(NULL));
        TEST_ASSERT_NULL(ops->tx_ring(NULL));
        TEST_ASSERT_EQUAL_UINT32(0u, ops->baud_rate(NULL));
        uart_backend_stats_t stats = ops->stats(NULL);
        TEST_ASSERT_EQUAL_UINT32(0u, stats.uart_tx_bytes);
        TEST_ASSERT_EQUAL_UINT32(0u, stats.uart_rx_bytes);
        TEST_ASSERT_EQUAL_UINT32(0u, stats.rx_error_count);
        TEST_ASSERT_FALSE(ops->line_coding_matches(NULL, &request));
        TEST_ASSERT_FALSE(ops->line_coding_matches(NULL, NULL));
        TEST_ASSERT_FALSE(ops->line_coding_acceptable(NULL));
        TEST_ASSERT_FALSE(ops->set_line_coding(NULL, &request));
        TEST_ASSERT_FALSE(ops->set_line_coding(NULL, NULL));
        TEST_ASSERT_FALSE(ops->restart(NULL));
    }
    hardware.initialized = true;
    pio.initialized = true;
    TEST_ASSERT_FALSE(uart_backend_hw_ops.line_coding_matches(&hardware, NULL));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.line_coding_matches(&pio, NULL));
    TEST_ASSERT_FALSE(uart_backend_hw_ops.set_line_coding(&hardware, NULL));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, NULL));
    TEST_ASSERT_EQUAL_UINT(0u, clock_queries);
    TEST_ASSERT_EQUAL_UINT(0u, apply_calls);
}

void test_matching_requires_initialized_instances(void)
{
    uart_driver_line_coding_t request = {115200u, 8u, 1u, UART_DRIVER_PARITY_NONE};
    hardware.baud_rate = 115207u;
    hardware.data_bits = 8u;
    hardware.stop_bits = 1u;
    hardware.parity = UART_PARITY_NONE;
    pio.baud_rate = request.baud_rate;
    TEST_ASSERT_FALSE(uart_backend_hw_ops.line_coding_matches(&hardware, &request));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.line_coding_matches(&pio, &request));
    TEST_ASSERT_EQUAL_UINT(0u, clock_queries);
}

void test_infeasible_requests_never_reach_hardware_application(void)
{
    uart_driver_line_coding_t request = {115200u, 8u, 1u, UART_DRIVER_PARITY_NONE};
    hardware.initialized = true;
    pio.initialized = true;
    peripheral_hz = 1000000u;
    TEST_ASSERT_FALSE(uart_backend_hw_ops.line_coding_acceptable(&request));
    TEST_ASSERT_FALSE(uart_backend_hw_ops.set_line_coding(&hardware, &request));
    system_hz = 24000000u;
    request.baud_rate = 3000000u;
    pio.baud_rate = request.baud_rate;
    TEST_ASSERT_FALSE(uart_backend_pio_ops.line_coding_matches(&pio, &request));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.line_coding_acceptable(&request));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, &request));
    TEST_ASSERT_EQUAL_UINT(0u, apply_calls);
}

void test_binding_rejects_invalid_records_without_changing_state(void)
{
    uart_driver_port_info_t copy = uart_board_ports[UART_PORT_0];
    TEST_ASSERT_FALSE(uart_backend_hw_ops.load_config(NULL, &uart_board_ports[UART_PORT_0]));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.load_config(NULL, &uart_board_ports[UART_PORT_2]));
    TEST_ASSERT_FALSE(uart_backend_hw_ops.load_config(&hardware, NULL));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.load_config(&pio, NULL));
    TEST_ASSERT_FALSE(uart_backend_hw_ops.load_config(&hardware, &copy));
    copy = uart_board_ports[UART_PORT_2];
    TEST_ASSERT_FALSE(uart_backend_pio_ops.load_config(&pio, &copy));
    copy.id = UART_PORT_COUNT;
    TEST_ASSERT_FALSE(uart_backend_hw_ops.load_config(&hardware, &copy));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.load_config(&pio, &copy));
    copy.id = (uart_port_id_t)-1;
    TEST_ASSERT_FALSE(uart_backend_hw_ops.load_config(&hardware, &copy));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.load_config(&pio, &copy));
    TEST_ASSERT_FALSE(uart_backend_hw_ops.load_config(&hardware, &uart_board_ports[UART_PORT_2]));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.load_config(&pio, &uart_board_ports[UART_PORT_0]));
    TEST_ASSERT_NULL(hardware.config);
    TEST_ASSERT_NULL(pio.config);
    TEST_ASSERT_EQUAL_UINT32(0u, hardware.baud_rate);
    TEST_ASSERT_EQUAL_UINT32(0u, pio.baud_rate);
    TEST_ASSERT_EQUAL_UINT(0u, clock_queries);
    TEST_ASSERT_EQUAL_UINT(0u, apply_calls);
}

void test_binding_uses_static_descriptors_and_refuses_active_rebinding(void)
{
    TEST_ASSERT_TRUE(uart_backend_hw_ops.load_config(&hardware, &uart_board_ports[UART_PORT_0]));
    TEST_ASSERT_TRUE(uart_backend_pio_ops.load_config(&pio, &uart_board_ports[UART_PORT_2]));
    TEST_ASSERT_EQUAL_PTR(&hw_uart_driver_configs[UART_PORT_0], hardware.config);
    TEST_ASSERT_EQUAL_PTR(&pio_uart_driver_configs[UART_PORT_2], pio.config);
    TEST_ASSERT_EQUAL_UINT32(115200u, hardware.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(8u, hardware.data_bits);
    TEST_ASSERT_EQUAL_UINT8(1u, hardware.stop_bits);
    TEST_ASSERT_EQUAL_UINT(UART_PARITY_NONE, hardware.parity);
    TEST_ASSERT_EQUAL_UINT32(115200u, pio.baud_rate);
    hardware.initialized = true;
    pio.initialized = true;
    hardware.baud_rate = 230400u;
    pio.baud_rate = 230400u;
    TEST_ASSERT_FALSE(uart_backend_hw_ops.load_config(&hardware, &uart_board_ports[UART_PORT_1]));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.load_config(&pio, &uart_board_ports[UART_PORT_3]));
    TEST_ASSERT_EQUAL_PTR(&hw_uart_driver_configs[UART_PORT_0], hardware.config);
    TEST_ASSERT_EQUAL_PTR(&pio_uart_driver_configs[UART_PORT_2], pio.config);
    TEST_ASSERT_EQUAL_UINT32(230400u, hardware.baud_rate);
    TEST_ASSERT_EQUAL_UINT32(230400u, pio.baud_rate);
    TEST_ASSERT_EQUAL_UINT(0u, clock_queries);
    TEST_ASSERT_EQUAL_UINT(0u, apply_calls);
}

void test_setters_require_initialized_instances(void)
{
    uart_driver_line_coding_t request = {115200u, 8u, 1u, UART_DRIVER_PARITY_NONE};
    TEST_ASSERT_FALSE(uart_backend_hw_ops.set_line_coding(NULL, &request));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(NULL, &request));
    TEST_ASSERT_FALSE(uart_backend_hw_ops.set_line_coding(&hardware, &request));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, &request));
    TEST_ASSERT_EQUAL_UINT(0u, apply_calls);
}

void test_real_callbacks_return_driver_rings_rates_and_counters(void)
{
    hardware.initialized = true;
    hardware.baud_rate = 115207u;
    hardware.uart_tx_bytes = 13u;
    hardware.uart_rx_bytes = 19u;
    hardware.rx_error_count = 2u;
    pio.baud_rate = 230400u;
    pio.tx_polled_bytes = 17u;
    pio.tx_dma_bytes = 23u;
    pio.uart_rx_bytes = 29u;
    pio.rx_error_count = 3u;
    TEST_ASSERT_TRUE(uart_backend_hw_ops.is_initialized(&hardware));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.is_initialized(&pio));
    TEST_ASSERT_EQUAL_PTR(&hardware.rx_ring, uart_backend_hw_ops.rx_ring(&hardware));
    TEST_ASSERT_EQUAL_PTR(&hardware.tx_ring, uart_backend_hw_ops.tx_ring(&hardware));
    TEST_ASSERT_EQUAL_PTR(&pio.rx_ring, uart_backend_pio_ops.rx_ring(&pio));
    TEST_ASSERT_EQUAL_PTR(&pio.tx_ring, uart_backend_pio_ops.tx_ring(&pio));
    TEST_ASSERT_EQUAL_UINT32(115207u, uart_backend_hw_ops.baud_rate(&hardware));
    TEST_ASSERT_EQUAL_UINT32(230400u, uart_backend_pio_ops.baud_rate(&pio));
    uart_backend_stats_t stats = uart_backend_hw_ops.stats(&hardware);
    TEST_ASSERT_EQUAL_UINT32(13u, stats.uart_tx_bytes);
    TEST_ASSERT_EQUAL_UINT32(19u, stats.uart_rx_bytes);
    TEST_ASSERT_EQUAL_UINT32(2u, stats.rx_error_count);
    stats = uart_backend_pio_ops.stats(&pio);
    TEST_ASSERT_EQUAL_UINT32(40u, stats.uart_tx_bytes);
    TEST_ASSERT_EQUAL_UINT32(29u, stats.uart_rx_bytes);
    TEST_ASSERT_EQUAL_UINT32(3u, stats.rx_error_count);
}

void test_stopped_backends_stage_settings_before_hardware_start(void)
{
    hw_uart_driver_t hardware = {0};
    pio_uart_driver_t pio = {0};
    uart_driver_line_coding_t coding = {
        .baud_rate = 230400u, .data_bits = 8u, .stop_bits = 1u, .parity = UART_DRIVER_PARITY_NONE,
    };
    TEST_ASSERT_FALSE(uart_backend_hw_ops.set_line_coding(&hardware, &coding));
    TEST_ASSERT_FALSE(uart_backend_pio_ops.set_line_coding(&pio, &coding));
    TEST_ASSERT_TRUE(uart_backend_hw_ops.load_config(&hardware, &uart_board_ports[UART_PORT_0]));
    TEST_ASSERT_TRUE(uart_backend_pio_ops.load_config(&pio, &uart_board_ports[UART_PORT_2]));
    TEST_ASSERT_TRUE(uart_backend_hw_ops.set_line_coding(&hardware, &coding));
    TEST_ASSERT_TRUE(uart_backend_pio_ops.set_line_coding(&pio, &coding));
    TEST_ASSERT_EQUAL_UINT32(230400u, hardware.baud_rate);
    TEST_ASSERT_EQUAL_UINT32(230400u, pio.baud_rate);
    TEST_ASSERT_FALSE(hardware.initialized);
    TEST_ASSERT_FALSE(pio.initialized);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stopped_backends_stage_settings_before_hardware_start);
    RUN_TEST(test_real_tables_are_complete_and_selected);
    RUN_TEST(test_pio_reinitialization_preserves_resource_ownership_and_state);
    RUN_TEST(test_hw_reinitialization_preserves_resource_ownership_and_state);
    RUN_TEST(test_invalid_requests_never_reach_hardware_application);
    RUN_TEST(test_hardware_setter_translates_all_supported_parities_and_propagates_failure);
    RUN_TEST(test_hardware_matching_uses_applied_divider_rate_and_peripheral_clock);
    RUN_TEST(test_pio_setter_rejects_non_8n1_and_rx_divider_limits);
    RUN_TEST(test_setters_require_initialized_instances);
    RUN_TEST(test_null_api_inputs_return_neutral_results_without_hardware_access);
    RUN_TEST(test_matching_requires_initialized_instances);
    RUN_TEST(test_infeasible_requests_never_reach_hardware_application);
    RUN_TEST(test_binding_rejects_invalid_records_without_changing_state);
    RUN_TEST(test_binding_uses_static_descriptors_and_refuses_active_rebinding);
    RUN_TEST(test_real_callbacks_return_driver_rings_rates_and_counters);
    return UNITY_END();
}