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
#include "uart/uart_driver.h"
#include "uart/worker.h"
#ifdef PICO_UART_CDC_INTEGRATION
#include "tusb.h"
#include "usb/usb_cdc.h"
#endif

#include <string.h>

absolute_time_t pico_test_time_us;
static uint32_t test_stats_call_count;
static void *test_startup_contexts[UART_PORT_COUNT];
static bool test_startup_initialized[UART_PORT_COUNT];
static size_t test_startup_init_calls;
static size_t test_startup_deinit_calls;
static size_t test_startup_baseline_calls;
static size_t test_multicore_launch_calls;
static size_t test_multicore_reset_calls;
static size_t test_startup_fail_at;
static ring_buffer_t test_startup_rings[UART_PORT_COUNT];
static uint8_t test_startup_storage[UART_PORT_COUNT][128];
static uart_driver_line_coding_t test_backend_coding[UART_PORT_COUNT];
static const uart_worker_hooks_t *test_worker_hooks;
static size_t test_control_service_calls;
static size_t test_reset_calls;
static bool test_reset_fails;
static bool test_close_during_start;
static bool test_reopen_during_start;
static bool test_reject_during_start;
static bool test_stats_interference;

const uart_driver_port_info_t uart_board_ports[UART_PORT_COUNT] = {
    [UART_PORT_0] = {.id = UART_PORT_0, .backend = UART_DRIVER_BACKEND_HW, .baud_rate = 115200u,
                     .data_bits = 8u, .stop_bits = 1u},
    [UART_PORT_1] = {.id = UART_PORT_1, .backend = UART_DRIVER_BACKEND_HW, .baud_rate = 115200u,
                     .data_bits = 8u, .stop_bits = 1u},
    [UART_PORT_2] = {.id = UART_PORT_2, .backend = UART_DRIVER_BACKEND_PIO, .baud_rate = 115200u,
                     .data_bits = 8u, .stop_bits = 1u},
    [UART_PORT_3] = {.id = UART_PORT_3, .backend = UART_DRIVER_BACKEND_PIO, .baud_rate = 115200u,
                     .data_bits = 8u, .stop_bits = 1u},
    [UART_PORT_4] = {.id = UART_PORT_4, .backend = UART_DRIVER_BACKEND_PIO, .baud_rate = 115200u,
                     .data_bits = 8u, .stop_bits = 1u},
    [UART_PORT_5] = {.id = UART_PORT_5, .backend = UART_DRIVER_BACKEND_PIO, .baud_rate = 115200u,
                     .data_bits = 8u, .stop_bits = 1u},
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
    test_backend_coding[port_info->id] = (uart_driver_line_coding_t){
        .baud_rate = port_info->baud_rate, .data_bits = 8u, .stop_bits = 1u, .parity = UART_DRIVER_PARITY_NONE,
    };
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
    size_t index = test_startup_port_index(instance);
    return (index < UART_PORT_COUNT) && (line_coding != NULL) &&
           test_backend_coding[index].baud_rate == line_coding->baud_rate &&
           test_backend_coding[index].data_bits == line_coding->data_bits &&
           test_backend_coding[index].stop_bits == line_coding->stop_bits &&
           test_backend_coding[index].parity == line_coding->parity;
}

static bool test_startup_line_coding_acceptable(const uart_driver_line_coding_t *line_coding)
{
    return line_coding != NULL;
}

static bool test_startup_set_line_coding(void *instance, const uart_driver_line_coding_t *line_coding)
{
    size_t index = test_startup_port_index(instance);
    if ((index >= UART_PORT_COUNT) || (line_coding == NULL)) return false;
    test_backend_coding[index] = *line_coding;
    return true;
}

static bool test_startup_restart(void *instance)
{
    size_t index = test_startup_port_index(instance);
    if (index >= UART_PORT_COUNT) {
        return false;
    }
    ++test_reset_calls;
    TEST_ASSERT_TRUE(ring_buffer_init(&test_startup_rings[index], test_startup_storage[index],
                                     sizeof(test_startup_storage[index])));
    test_startup_initialized[index] = !test_reset_fails;
    if (test_close_during_start) {
        uart_driver_request_channel_stop((uart_port_id_t)index);
        if (test_reopen_during_start) {
            uart_driver_request_channel_start((uart_port_id_t)index);
        }
    }
    if (test_reject_during_start) {
        uart_driver_report_control_error((uart_port_id_t)index, UART_CONTROL_ERROR_OTHER);
    }
    return (instance != NULL) && !test_reset_fails;
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
    size_t index = test_startup_port_index(instance);
    return index < UART_PORT_COUNT ? test_backend_coding[index].baud_rate : 0u;
}

static uart_backend_stats_t test_startup_stats(const void *instance)
{
    (void)instance;
    ++test_stats_call_count;
    if (test_stats_interference) {
        test_worker_hooks->poll_io();
    }
    return (uart_backend_stats_t){.uart_tx_bytes = 123u, .uart_rx_bytes = 456u};
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
    .restart = test_startup_restart,
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
    .restart = test_startup_restart,
    .rx_snapshot_is_current = test_startup_rx_snapshot_is_current,
    .clear_rx_error_baseline = test_startup_clear_rx_error_baseline,
    .baud_rate = test_startup_baud_rate,
    .stats = test_startup_stats,
};

static void test_record_multicore_reset(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, test_multicore_launch_calls);
    ++test_multicore_reset_calls;
}

static void test_record_multicore_launch(pico_test_core_entry_t entry)
{
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT(1u, test_multicore_reset_calls);
    ++test_multicore_launch_calls;
    entry();
}

void uart_worker_run(const uart_worker_hooks_t *hooks)
{
    test_worker_hooks = hooks;
}

void hw_uart_driver_enable_rx_dma_irq(void)
{
}

void pio_uart_driver_enable_rx_dma_irq(void)
{
}

#ifndef PICO_UART_CDC_INTEGRATION
void uart_control_plane_service(uart_control_plane_t *control_plane)
{
    (void)control_plane;
    ++test_control_service_calls;
}

bool uart_control_plane_start_port(uart_control_plane_t *control_plane, uart_port_id_t port_id)
{
    uart_runtime_port_t *port = &control_plane->ports[port_id];
    return port->ops->restart(&port->backend);
}

bool uart_control_plane_tx_launch_allowed(const uart_control_plane_t *control_plane, uart_port_id_t port_id)
{
    (void)control_plane;
    (void)port_id;
    return false;
}
#endif

#ifdef PICO_UART_CDC_INTEGRATION
static bool test_usb_mounted;
static uint32_t test_usb_received;
void tusb_init(void) {}
bool tud_disconnect(void) { return true; }
bool tud_connect(void) { return true; }
bool tud_task_event_ready(void) { return false; }
void tud_task(void) {}
bool tud_mounted(void) { return test_usb_mounted; }
uint32_t tud_cdc_n_available(uint8_t itf) { (void)itf; return 0u; }
uint32_t tud_cdc_n_read(uint8_t itf, void *buffer, uint32_t length)
{ (void)itf; (void)buffer; (void)length; return 0u; }
void tud_cdc_n_read_flush(uint8_t itf) { (void)itf; }
uint32_t tud_cdc_n_write(uint8_t itf, const void *buffer, uint32_t length)
{ (void)itf; (void)buffer; test_usb_received += length; return length; }
uint32_t tud_cdc_n_write_available(uint8_t itf) { (void)itf; return 64u; }
uint32_t tud_cdc_n_write_flush(uint8_t itf) { (void)itf; return 0u; }
bool tud_cdc_n_write_clear(uint8_t itf) { (void)itf; return true; }
void usb_hid_reset_host_state(void) {}

static void test_cdc_open(uint32_t baud, uint8_t stop_bits, uint8_t parity)
{
    cdc_line_coding_t coding = {.bit_rate = baud, .data_bits = 8u, .stop_bits = stop_bits, .parity = parity};
    tud_cdc_line_coding_cb(0u, &coding);
    tud_cdc_line_state_cb(0u, true, false);
    usb_cdc_poll();
}

static void test_cdc_close(void)
{
    tud_cdc_line_state_cb(0u, false, false);
    usb_cdc_poll();
    test_worker_hooks->service_control();
}

void test_integrated_start_publishes_matching_framing_before_rx(void)
{
    test_cdc_open(230400u, 2u, 1u);
    TEST_ASSERT_FALSE(uart_driver_port_is_ready(UART_PORT_0));
    TEST_ASSERT_EQUAL_UINT32(0u, test_usb_received);
    test_worker_hooks->service_control();
    uart_driver_port_info_t info;
    TEST_ASSERT_TRUE(uart_driver_port_info(UART_PORT_0, &info));
    TEST_ASSERT_TRUE(uart_driver_port_is_ready(UART_PORT_0));
    TEST_ASSERT_EQUAL_UINT32(test_backend_coding[0].baud_rate, info.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(test_backend_coding[0].stop_bits, info.stop_bits);
    TEST_ASSERT_EQUAL_UINT8(test_backend_coding[0].parity, info.parity);
    TEST_ASSERT_EQUAL_UINT8(2u, info.stop_bits);
    TEST_ASSERT_EQUAL_UINT8(UART_DRIVER_PARITY_ODD, info.parity);
    const uint8_t payload[] = {1u, 2u, 3u};
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), ring_buffer_write(&test_startup_rings[0], payload, sizeof(payload)));
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), test_usb_received);
    test_cdc_close();
}

void test_integrated_open_without_new_settings_uses_board_defaults(void)
{
    tud_cdc_line_state_cb(1u, true, false);
    usb_cdc_poll();
    test_worker_hooks->service_control();
    usb_cdc_poll();
    TEST_ASSERT_TRUE(uart_driver_port_is_ready(UART_PORT_1));
    TEST_ASSERT_EQUAL_UINT32(115200u, test_backend_coding[1].baud_rate);
    TEST_ASSERT_EQUAL_UINT8(8u, test_backend_coding[1].data_bits);
    TEST_ASSERT_EQUAL_UINT8(1u, test_backend_coding[1].stop_bits);
    tud_cdc_line_state_cb(1u, false, false);
    usb_cdc_poll();
    test_worker_hooks->service_control();
}

void test_integrated_failed_start_and_reopen_without_new_line_coding(void)
{
    test_cdc_open(115200u, 0u, 0u);
    test_worker_hooks->service_control();
    usb_cdc_poll();
    test_cdc_close();
    test_cdc_open(230400u, 2u, 1u);
    test_reset_fails = true;
    test_worker_hooks->service_control();
    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_INIT_FAILED, uart_driver_port_status(UART_PORT_0));
    TEST_ASSERT_FALSE(uart_driver_port_is_ready(UART_PORT_0));
    test_reset_fails = false;
    test_cdc_close();
    tud_cdc_line_state_cb(0u, true, false);
    usb_cdc_poll();
    test_worker_hooks->service_control();
    usb_cdc_poll();
    uart_driver_port_info_t info;
    TEST_ASSERT_TRUE(uart_driver_port_info(UART_PORT_0, &info));
    TEST_ASSERT_TRUE(uart_driver_port_is_ready(UART_PORT_0));
    TEST_ASSERT_EQUAL_UINT32(115200u, test_backend_coding[0].baud_rate);
    TEST_ASSERT_EQUAL_UINT8(1u, test_backend_coding[0].stop_bits);
    TEST_ASSERT_EQUAL_UINT8(UART_DRIVER_PARITY_NONE, test_backend_coding[0].parity);
    TEST_ASSERT_EQUAL_UINT32(test_backend_coding[0].baud_rate, info.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(test_backend_coding[0].stop_bits, info.stop_bits);
    TEST_ASSERT_EQUAL_UINT8(test_backend_coding[0].parity, info.parity);
    test_cdc_close();
}

void test_integrated_bus_reset_stops_backend_and_discards_old_rx(void)
{
    test_cdc_open(115200u, 0u, 0u);
    test_worker_hooks->service_control();
    usb_cdc_poll();
    const uint8_t old_payload[] = {7u, 8u};
    TEST_ASSERT_EQUAL_UINT(sizeof(old_payload), ring_buffer_write(&test_startup_rings[0], old_payload, sizeof(old_payload)));
    test_usb_mounted = false;
    usb_cdc_poll();
    test_worker_hooks->service_control();
    TEST_ASSERT_FALSE(test_startup_initialized[0]);
    TEST_ASSERT_FALSE(uart_driver_port_is_ready(UART_PORT_0));
    test_usb_mounted = true;
    tud_mount_cb();
    test_cdc_open(115200u, 0u, 0u);
    test_worker_hooks->service_control();
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(0u, test_usb_received);
    test_cdc_close();
}
#endif

void setUp(void)
{
    test_stats_call_count = 0u;
    test_multicore_launch_hook = NULL;
    test_multicore_reset_hook = NULL;
    test_reset_fails = false;
    test_close_during_start = false;
    test_reopen_during_start = false;
    test_reject_during_start = false;
    test_stats_interference = false;
#ifdef PICO_UART_CDC_INTEGRATION
    test_usb_mounted = true;
    test_usb_received = 0u;
    tud_mount_cb();
    if (test_worker_hooks != NULL) test_worker_hooks->service_control();
#endif
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
    TEST_ASSERT_EQUAL_UINT(0u, uart_driver_drain_rx(UART_PORT_COUNT, 1u, NULL, NULL, NULL, 0u));
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

    uart_driver_report_control_error(UART_PORT_0, UART_CONTROL_ERROR_OTHER);
    uart_driver_report_soft_pending_error(UART_PORT_0, 1u, UART_CONTROL_ERROR_TIMEOUT);
    uart_driver_reset_soft_pending(UART_PORT_0);

    TEST_ASSERT_EQUAL_UINT8(0u, uart_driver_port_status(UART_PORT_0));
}

void test_uart_driver_rejects_stats_from_stopped_backend(void)
{
    uart_driver_port_stats_t stats;

    TEST_ASSERT_FALSE(uart_driver_port_stats(UART_PORT_0, &stats));
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
    test_multicore_reset_calls = 0u;
    test_startup_fail_at = 3u;
    test_multicore_launch_hook = test_record_multicore_launch;
    test_multicore_reset_hook = test_record_multicore_reset;

    TEST_ASSERT_FALSE(uart_driver_init());
    TEST_ASSERT_EQUAL_UINT(UART_PORT_COUNT, test_startup_init_calls);
    TEST_ASSERT_EQUAL_UINT(UART_PORT_COUNT - 1u, test_startup_deinit_calls);
    TEST_ASSERT_EQUAL_UINT(0u, test_startup_baseline_calls);
    TEST_ASSERT_EQUAL_UINT(0u, test_multicore_launch_calls);
    TEST_ASSERT_EQUAL_UINT(0u, test_multicore_reset_calls);
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
    TEST_ASSERT_EQUAL_UINT(UART_PORT_COUNT, test_startup_deinit_calls);
    TEST_ASSERT_EQUAL_UINT(UART_PORT_COUNT, test_startup_baseline_calls);
    TEST_ASSERT_EQUAL_UINT(1u, test_multicore_launch_calls);
    TEST_ASSERT_EQUAL_UINT(1u, test_multicore_reset_calls);
    TEST_ASSERT_TRUE(uart_driver_worker_is_running());
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        TEST_ASSERT_FALSE(test_startup_initialized[index]);
        TEST_ASSERT_FALSE(uart_driver_port_is_ready((uart_port_id_t)index));
    }

    TEST_ASSERT_TRUE(uart_driver_init());
    TEST_ASSERT_EQUAL_UINT(1u, test_multicore_launch_calls);
    TEST_ASSERT_EQUAL_UINT(1u, test_multicore_reset_calls);

    TEST_ASSERT_TRUE(uart_driver_request_channel_start(UART_PORT_2));
    TEST_ASSERT_TRUE(uart_driver_channel_start_pending(UART_PORT_2));
    TEST_ASSERT_FALSE(uart_driver_port_is_ready(UART_PORT_2));
    TEST_ASSERT_TRUE(uart_driver_request_channel_stop(UART_PORT_2));
    TEST_ASSERT_FALSE(uart_driver_channel_start_pending(UART_PORT_2));
    TEST_ASSERT_TRUE(uart_driver_channel_stop_pending(UART_PORT_2));
    TEST_ASSERT_FALSE(uart_driver_channel_stop_is_quiesced(UART_PORT_2));
    uart_driver_channel_stop_usb_quiesced(UART_PORT_2);
    TEST_ASSERT_TRUE(uart_driver_channel_stop_is_quiesced(UART_PORT_2));
    TEST_ASSERT_FALSE(uart_driver_request_channel_stop(UART_PORT_2));
    TEST_ASSERT_TRUE(uart_driver_channel_stop_is_quiesced(UART_PORT_2));
}

void test_failed_channel_start_finishes_without_retrying_forever(void)
{
    test_worker_hooks->service_control();
    TEST_ASSERT_TRUE(uart_driver_request_channel_start(UART_PORT_2));
    pico_test_time_us += 250000u;
    test_reset_fails = true;
    test_reset_calls = 0u;
    test_worker_hooks->service_control();

    TEST_ASSERT_EQUAL_UINT(1u, test_reset_calls);
    TEST_ASSERT_FALSE(uart_driver_channel_start_pending(UART_PORT_2));
    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_INIT_FAILED, uart_driver_port_status(UART_PORT_2));
    TEST_ASSERT_FALSE(uart_driver_port_is_ready(UART_PORT_2));
    test_control_service_calls = 0u;
    test_worker_hooks->service_control();
    TEST_ASSERT_EQUAL_UINT(1u, test_control_service_calls);
    TEST_ASSERT_EQUAL_UINT(1u, test_reset_calls);
    test_reset_fails = false;
}

void test_rx_storage_is_distinct_aligned_and_does_not_pad_runtime_ports(void)
{
    TEST_ASSERT_TRUE(_Alignof(uart_runtime_port_t) < PICO_UART_HW_UART_RX_BUFFER_SIZE);
    TEST_ASSERT_TRUE(sizeof(uart_runtime_port_t) <
                     PICO_UART_HW_UART_TX_BUFFER_SIZE + PICO_UART_PIO_UART_TX_BUFFER_SIZE);
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_backend_instance_t *backend = test_startup_contexts[index];
        uint8_t *storage = uart_board_ports[index].backend == UART_DRIVER_BACKEND_HW ?
                           backend->hw.rx_storage : backend->pio.rx_storage;
        size_t capacity = uart_board_ports[index].backend == UART_DRIVER_BACKEND_HW ?
                          PICO_UART_HW_UART_RX_BUFFER_SIZE : PICO_UART_PIO_UART_RX_BUFFER_SIZE;
        TEST_ASSERT_NOT_NULL(storage);
        TEST_ASSERT_EQUAL_UINT(0u, (uintptr_t)storage % capacity);
        for (size_t previous = 0u; previous < index; ++previous) {
            uart_backend_instance_t *other = test_startup_contexts[previous];
            uint8_t *other_storage = uart_board_ports[previous].backend == UART_DRIVER_BACKEND_HW ?
                                     other->hw.rx_storage : other->pio.rx_storage;
            size_t other_capacity = uart_board_ports[previous].backend == UART_DRIVER_BACKEND_HW ?
                                    PICO_UART_HW_UART_RX_BUFFER_SIZE : PICO_UART_PIO_UART_RX_BUFFER_SIZE;
            TEST_ASSERT_TRUE(((uintptr_t)storage + capacity <= (uintptr_t)other_storage) ||
                             ((uintptr_t)other_storage + other_capacity <= (uintptr_t)storage));
        }
    }
}

void test_close_during_start_does_not_publish_ready(void)
{
    uart_driver_port_stats_t stats;
    TEST_ASSERT_TRUE(uart_driver_request_channel_start(UART_PORT_2));
    pico_test_time_us += 250000u;
    test_close_during_start = true;
    test_worker_hooks->service_control();
    test_close_during_start = false;

    TEST_ASSERT_TRUE(uart_driver_channel_stop_pending(UART_PORT_2));
    TEST_ASSERT_BITS_LOW(UART_DRIVER_PORT_STATUS_READY, uart_driver_port_status(UART_PORT_2));
    TEST_ASSERT_FALSE(uart_driver_port_is_ready(UART_PORT_2));
    TEST_ASSERT_TRUE(uart_driver_port_stats(UART_PORT_2, &stats));
    uart_driver_channel_stop_usb_quiesced(UART_PORT_2);
    test_worker_hooks->service_control();
}

void test_reopen_during_start_keeps_the_new_start_request(void)
{
    TEST_ASSERT_TRUE(uart_driver_request_channel_start(UART_PORT_2));
    pico_test_time_us += 250000u;
    test_close_during_start = true;
    test_reopen_during_start = true;
    test_worker_hooks->service_control();
    test_close_during_start = false;

    TEST_ASSERT_TRUE(uart_driver_channel_start_pending(UART_PORT_2));
    TEST_ASSERT_BITS_LOW(UART_DRIVER_PORT_STATUS_READY, uart_driver_port_status(UART_PORT_2));
    TEST_ASSERT_FALSE(uart_driver_port_is_ready(UART_PORT_2));
    uart_driver_channel_stop_usb_quiesced(UART_PORT_2);
    test_reset_calls = 0u;
    test_worker_hooks->service_control();
    TEST_ASSERT_EQUAL_UINT(1u, test_reset_calls);
    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_READY, uart_driver_port_status(UART_PORT_2));
}

void test_close_completion_preserves_reopened_control_state(void)
{
    uint32_t old_generation = uart_driver_mark_control_pending(UART_PORT_2);
    TEST_ASSERT_TRUE(uart_driver_request_channel_stop(UART_PORT_2));
    uart_driver_channel_stop_usb_quiesced(UART_PORT_2);
    TEST_ASSERT_TRUE(uart_driver_request_channel_start(UART_PORT_2));
    TEST_ASSERT_EQUAL_UINT32(old_generation + 2u, uart_driver_mark_control_pending(UART_PORT_2));
    uart_driver_report_control_error(UART_PORT_2, UART_CONTROL_ERROR_FRAMING);
    test_worker_hooks->service_control();

    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_CONTROL_PENDING | UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                          uart_driver_port_status(UART_PORT_2));
    pico_test_time_us += 250000u;
    test_worker_hooks->service_control();
    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_CONTROL_PENDING | UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                          uart_driver_port_status(UART_PORT_2));
    uart_driver_reset_soft_pending(UART_PORT_2);
}

void test_start_completion_preserves_a_newer_control_rejection(void)
{
    TEST_ASSERT_TRUE(uart_driver_request_channel_stop(UART_PORT_2));
    uart_driver_channel_stop_usb_quiesced(UART_PORT_2);
    test_worker_hooks->service_control();
    TEST_ASSERT_TRUE(uart_driver_request_channel_start(UART_PORT_2));
    pico_test_time_us += 250000u;
    test_reject_during_start = true;
    test_worker_hooks->service_control();

    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_READY | UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                          uart_driver_port_status(UART_PORT_2));
}

void test_channel_stop_wait_does_not_block_another_start_or_control_service(void)
{
    TEST_ASSERT_TRUE(uart_driver_request_channel_stop(UART_PORT_1));
    TEST_ASSERT_TRUE(uart_driver_request_channel_start(UART_PORT_0));
    TEST_ASSERT_TRUE(uart_driver_request_channel_start(UART_PORT_1));
    test_control_service_calls = 0u;
    test_worker_hooks->service_control();

    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_READY, uart_driver_port_status(UART_PORT_0));
    TEST_ASSERT_TRUE(uart_driver_channel_start_pending(UART_PORT_1));
    TEST_ASSERT_EQUAL_UINT(1u, test_control_service_calls);
    uart_driver_channel_stop_usb_quiesced(UART_PORT_1);
    test_worker_hooks->service_control();
    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_READY, uart_driver_port_status(UART_PORT_1));
}

static uint32_t idle_unlock_count;

static void count_idle_unlock(void)
{
    ++idle_unlock_count;
}

void test_idle_lifecycle_sweep_requires_one_lock_per_port(void)
{
    idle_unlock_count = 0u;
    test_spin_unlock_hook = count_idle_unlock;
    test_worker_hooks->service_control();
    test_spin_unlock_hook = NULL;
    TEST_ASSERT_EQUAL_UINT32(UART_PORT_COUNT, idle_unlock_count);
}

void test_support_check_uses_immutable_backend_without_status_lock(void)
{
    const uart_driver_line_coding_t coding = {
        .baud_rate = 115200u, .data_bits = 8u, .stop_bits = 1u, .parity = UART_DRIVER_PARITY_NONE,
    };
    uart_driver_line_coding_t invalid = coding;
    invalid.baud_rate = 0u;
    idle_unlock_count = 0u;
    test_spin_unlock_hook = count_idle_unlock;
    TEST_ASSERT_TRUE(uart_driver_line_coding_acceptable(UART_PORT_0, &coding));
    TEST_ASSERT_FALSE(uart_driver_line_coding_acceptable(UART_PORT_COUNT, &coding));
    TEST_ASSERT_FALSE(uart_driver_line_coding_acceptable(UART_PORT_0, &invalid));
    TEST_ASSERT_FALSE(uart_driver_line_coding_acceptable(UART_PORT_0, NULL));
    test_spin_unlock_hook = NULL;
    TEST_ASSERT_EQUAL_UINT32(0u, idle_unlock_count);
}

void test_rejected_stats_preserve_output_and_stopped_stats_remain_coherent(void)
{
    TEST_ASSERT_TRUE(uart_driver_request_channel_start(UART_PORT_0));
    test_worker_hooks->service_control();
    uart_driver_port_stats_t stats;
    memset(&stats, 0xa5, sizeof(stats));
    uart_driver_port_stats_t original = stats;
    test_stats_interference = true;
    TEST_ASSERT_FALSE(uart_driver_port_stats(UART_PORT_0, &stats));
    TEST_ASSERT_EQUAL_MEMORY(&original, &stats, sizeof(stats));
    TEST_ASSERT_EQUAL_UINT(3u, test_stats_call_count);
    test_stats_interference = false;
    TEST_ASSERT_TRUE(uart_driver_port_stats(UART_PORT_0, &stats));
    TEST_ASSERT_EQUAL_UINT32(123u, stats.uart_tx_bytes);
    TEST_ASSERT_TRUE(uart_driver_request_channel_stop(UART_PORT_0));
    uart_driver_channel_stop_usb_quiesced(UART_PORT_0);
    test_worker_hooks->service_control();
    TEST_ASSERT_FALSE(uart_driver_port_is_ready(UART_PORT_0));
    TEST_ASSERT_TRUE(uart_driver_port_stats(UART_PORT_0, &stats));
    TEST_ASSERT_EQUAL_UINT32(456u, stats.uart_rx_bytes);
}

void test_control_history_records_repeated_errors_and_survives_success(void)
{
    uart_driver_control_history_t history;
    pico_test_time_us = 1000000u;
    uart_driver_report_control_error(UART_PORT_0, UART_CONTROL_ERROR_BAUD);
    TEST_ASSERT_TRUE(uart_driver_control_history(UART_PORT_0, &history));
    TEST_ASSERT_EQUAL_UINT8(UART_CONTROL_ERROR_BAUD, history.reason);
    TEST_ASSERT_EQUAL_UINT64(1000000u, history.uptime_us);
    pico_test_time_us = 2000000u;
    uart_driver_report_control_error(UART_PORT_0, UART_CONTROL_ERROR_FRAMING);
    (void)uart_driver_mark_control_pending(UART_PORT_0);
    uart_driver_reset_soft_pending(UART_PORT_0);
    TEST_ASSERT_TRUE(uart_driver_control_history(UART_PORT_0, &history));
    TEST_ASSERT_EQUAL_UINT8(UART_CONTROL_ERROR_FRAMING, history.reason);
    TEST_ASSERT_EQUAL_UINT64(2000000u, history.uptime_us);
    TEST_ASSERT_FALSE(uart_driver_control_history(UART_PORT_COUNT, &history));
    TEST_ASSERT_FALSE(uart_driver_control_history(UART_PORT_0, NULL));
}

int main(void)
{
    UNITY_BEGIN();
#ifdef PICO_UART_CDC_INTEGRATION
    RUN_TEST(test_startup_rolls_back_partial_backend_init_and_retries);
    RUN_TEST(test_rx_storage_is_distinct_aligned_and_does_not_pad_runtime_ports);
    RUN_TEST(test_integrated_start_publishes_matching_framing_before_rx);
    RUN_TEST(test_integrated_open_without_new_settings_uses_board_defaults);
    RUN_TEST(test_integrated_failed_start_and_reopen_without_new_line_coding);
    RUN_TEST(test_integrated_bus_reset_stops_backend_and_discards_old_rx);
#else
    RUN_TEST(test_uart_driver_exposes_the_six_port_contract);
    RUN_TEST(test_uart_driver_rejects_invalid_port_operations);
    RUN_TEST(test_uart_driver_rejects_null_public_arguments);
    RUN_TEST(test_uart_driver_handles_valid_ports_before_initialization);
    RUN_TEST(test_startup_rolls_back_partial_backend_init_and_retries);
    RUN_TEST(test_rx_storage_is_distinct_aligned_and_does_not_pad_runtime_ports);
    RUN_TEST(test_uart_driver_rejects_stats_from_stopped_backend);
    RUN_TEST(test_failed_channel_start_finishes_without_retrying_forever);
    RUN_TEST(test_close_during_start_does_not_publish_ready);
    RUN_TEST(test_reopen_during_start_keeps_the_new_start_request);
    RUN_TEST(test_close_completion_preserves_reopened_control_state);
    RUN_TEST(test_start_completion_preserves_a_newer_control_rejection);
    RUN_TEST(test_channel_stop_wait_does_not_block_another_start_or_control_service);
    RUN_TEST(test_idle_lifecycle_sweep_requires_one_lock_per_port);
    RUN_TEST(test_support_check_uses_immutable_backend_without_status_lock);
    RUN_TEST(test_rejected_stats_preserve_output_and_stopped_stats_remain_coherent);
    RUN_TEST(test_control_history_records_repeated_errors_and_survives_success);
#endif
    return UNITY_END();
}