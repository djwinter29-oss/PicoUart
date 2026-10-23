/**
 * @file test_usb_cdc.c
 * @brief Exercise production CDC callbacks with simulated UART ownership and time.
 */

#include "unity.h"
#include "tusb.h"
#include "pico/time.h"
#include "uart/uart_driver.h"
#include "usb/usb_cdc.h"
#include <string.h>

absolute_time_t pico_test_time_us;
static bool ready[UART_PORT_COUNT];
static bool stopping[UART_PORT_COUNT];
static bool quiesced[UART_PORT_COUNT];
static uint8_t flags[UART_PORT_COUNT];
static uint32_t generation[UART_PORT_COUNT];
static bool accept_request;
static uint32_t applied_count;
static uart_driver_line_coding_t applied;
static bool mounted;
static bool request_queued;
static uint32_t queued_generation;
static uart_driver_line_coding_t queued_coding;
static uint32_t rx_bridge_calls;
static uint32_t drain_bytes[UART_PORT_COUNT];
static uint32_t flush_calls[UART_PORT_COUNT];
static uint32_t read_flush_calls[UART_PORT_COUNT];
static bool write_clear_ready[UART_PORT_COUNT];

void tusb_init(void) {}
bool tud_disconnect(void) { return true; }
bool tud_connect(void) { return true; }
bool tud_task_event_ready(void) { return false; }
void tud_task(void) {}
bool tud_mounted(void) { return mounted; }
uint32_t tud_cdc_n_available(uint8_t itf) { (void)itf; return 0u; }
uint32_t tud_cdc_n_read(uint8_t itf, void *buffer, uint32_t length)
{ (void)itf; (void)buffer; (void)length; return 0u; }
void tud_cdc_n_read_flush(uint8_t itf) { ++read_flush_calls[itf]; }
uint32_t tud_cdc_n_write(uint8_t itf, const void *buffer, uint32_t length)
{ (void)itf; (void)buffer; return length; }
uint32_t tud_cdc_n_write_available(uint8_t itf) { (void)itf; return 64u; }
uint32_t tud_cdc_n_write_flush(uint8_t itf) { ++flush_calls[itf]; return 0u; }
bool tud_cdc_n_write_clear(uint8_t itf) { return write_clear_ready[itf]; }
void service_load_note(unsigned int core) { TEST_ASSERT_EQUAL_UINT(0u, core); }
void usb_hid_reset_host_state(void) {}

bool uart_driver_port_is_ready(uart_port_id_t port) { return ready[port]; }
uint8_t uart_driver_port_status(uart_port_id_t port) { return flags[port]; }
bool uart_driver_channel_stop_pending(uart_port_id_t port) { return stopping[port]; }
bool uart_driver_channel_stop_is_quiesced(uart_port_id_t port) { return stopping[port] && quiesced[port]; }
void uart_driver_channel_stop_usb_quiesced(uart_port_id_t port) { quiesced[port] = true; }
bool uart_driver_request_channel_stop(uart_port_id_t port)
{
    bool queued = !stopping[port];
    stopping[port] = true;
    ready[port] = false;
    flags[port] = 0u;
    ++generation[port];
    return queued;
}
bool uart_driver_request_channel_start(uart_port_id_t port) { ready[port] = false; return true; }
void uart_driver_reset_soft_pending(uart_port_id_t port)
{ flags[port] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING; }
uint32_t uart_driver_mark_control_pending(uart_port_id_t port)
{
    flags[port] = UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    return ++generation[port];
}
void uart_driver_report_control_error(uart_port_id_t port)
{
    ++generation[port];
    flags[port] |= UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
}
void uart_driver_report_soft_pending_error(uart_port_id_t port, uint32_t request_generation)
{
    if (request_generation == generation[port]) {
        flags[port] = UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    }
}
bool uart_driver_line_coding_acceptable(uart_port_id_t port, const uart_driver_line_coding_t *coding)
{ (void)port; return coding != NULL && coding->data_bits == 8u; }

static void complete_request(uart_port_id_t port)
{
    if (!request_queued) return;
    applied = queued_coding;
    ++applied_count;
    request_queued = false;
    flags[port] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    if (queued_generation == generation[port]) flags[port] = 0u;
}

bool uart_driver_queue_line_coding(uart_port_id_t port, const uart_driver_line_coding_t *coding,
                                   uint32_t request_generation)
{
    if (!accept_request) return false;
    queued_coding = *coding;
    queued_generation = request_generation;
    request_queued = true;
    if (ready[port]) complete_request(port);
    return true;
}
size_t uart_driver_fill_tx(uart_port_id_t port, size_t capacity,
                           uint32_t (*reader)(void *, uint8_t *, uint32_t), void *context)
{ (void)port; (void)capacity; (void)reader; (void)context; return 0u; }
size_t uart_driver_drain_rx(uart_port_id_t port, size_t capacity,
                            uint32_t (*writer)(void *, const uint8_t *, uint32_t), void *context)
{
    uint8_t data[64] = {0};
    uint32_t length = drain_bytes[port];
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(data), length);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(capacity, length);
    drain_bytes[port] = 0u;
    ++rx_bridge_calls;
    return length != 0u ? writer(context, data, length) : 0u;
}
size_t uart_driver_recover_rx(uart_port_id_t port) { (void)port; return 0u; }

static void request_baud(uint32_t baud)
{
    cdc_line_coding_t coding = {.bit_rate = baud, .data_bits = 8u};
    tud_cdc_line_coding_cb(0u, &coding);
}

static void open_ready(void)
{
    tud_cdc_line_state_cb(0u, true, false);
    usb_cdc_poll();
    ready[0] = true;
    complete_request(UART_PORT_0);
    usb_cdc_poll();
}

void setUp(void)
{
    usb_cdc_reset_host_state();
    memset(ready, 0, sizeof(ready));
    memset(stopping, 0, sizeof(stopping));
    memset(quiesced, 0, sizeof(quiesced));
    memset(flags, 0, sizeof(flags));
    memset(generation, 0, sizeof(generation));
    applied_count = 0u;
    applied = (uart_driver_line_coding_t){0};
    accept_request = true;
    request_queued = false;
    rx_bridge_calls = 0u;
    memset(drain_bytes, 0, sizeof(drain_bytes));
    memset(flush_calls, 0, sizeof(flush_calls));
    memset(read_flush_calls, 0, sizeof(read_flush_calls));
    for (size_t port = 0u; port < UART_PORT_COUNT; ++port) {
        write_clear_ready[port] = true;
    }
    pico_test_time_us = 0;
    mounted = true;
    tud_mount_cb();
}
void tearDown(void) {}

void test_closed_configuration_waits_for_open_and_ready(void)
{
    request_baud(230400u);
    pico_test_time_us = 5000000;
    usb_cdc_poll();
    tud_cdc_line_state_cb(0u, true, false);
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(0u, applied_count);
    TEST_ASSERT_BITS_LOW(UART_DRIVER_PORT_STATUS_CONTROL_ERROR, flags[0]);
    pico_test_time_us += 5000000;
    ready[0] = true;
    complete_request(UART_PORT_0);
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, applied_count);
    TEST_ASSERT_EQUAL_UINT32(230400u, applied.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(0u, flags[0]);
}

void test_identical_retry_does_not_extend_mailbox_timeout(void)
{
    open_ready();
    accept_request = false;
    request_baud(115200u);
    usb_cdc_poll();
    pico_test_time_us = 900000;
    request_baud(115200u);
    usb_cdc_poll();
    pico_test_time_us = 1000001;
    usb_cdc_poll();
    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_CONTROL_ERROR, flags[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, applied_count);
}

void test_distinct_replacement_gets_its_own_deadline(void)
{
    open_ready();
    accept_request = false;
    request_baud(115200u);
    usb_cdc_poll();
    pico_test_time_us = 900000;
    request_baud(230400u);
    usb_cdc_poll();
    pico_test_time_us = 1100000;
    accept_request = true;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(230400u, applied.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(0u, flags[0]);
}

void test_close_cancels_old_request_and_preserves_reopened_request(void)
{
    open_ready();
    accept_request = false;
    request_baud(115200u);
    usb_cdc_poll();
    tud_cdc_line_state_cb(0u, false, false);
    tud_cdc_line_state_cb(0u, true, false);
    request_baud(230400u);
    pico_test_time_us = 5000000;
    usb_cdc_poll();
    TEST_ASSERT_TRUE(quiesced[0]);
    stopping[0] = false;
    ready[0] = true;
    accept_request = true;
    usb_cdc_poll();
    ready[0] = true;
    complete_request(UART_PORT_0);
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, applied_count);
    TEST_ASSERT_EQUAL_UINT32(230400u, applied.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(0u, flags[0]);
}

void test_disconnect_cancels_pending_configuration(void)
{
    open_ready();
    accept_request = false;
    request_baud(115200u);
    usb_cdc_poll();
    tud_umount_cb();
    stopping[0] = false;
    pico_test_time_us = 5000000;
    tud_mount_cb();
    tud_cdc_line_state_cb(0u, true, false);
    ready[0] = true;
    accept_request = true;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(0u, applied_count);
    TEST_ASSERT_EQUAL_UINT8(0u, flags[0]);
}

void test_unsupported_replacement_does_not_erase_prior_valid_request(void)
{
    open_ready();
    accept_request = false;
    request_baud(115200u);
    usb_cdc_poll();
    cdc_line_coding_t rejected = {.bit_rate = 230400u, .data_bits = 7u};
    tud_cdc_line_coding_cb(0u, &rejected);
    accept_request = true;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(115200u, applied.baud_rate);
    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_CONTROL_ERROR, flags[0]);
    TEST_ASSERT_BITS_LOW(UART_DRIVER_PORT_STATUS_CONTROL_PENDING, flags[0]);
}

void test_remount_without_unmount_stops_old_backend(void)
{
    tud_cdc_line_state_cb(0u, true, false);
    ready[0] = true;
    tud_mount_cb();
    usb_cdc_port_stats_t stats;
    TEST_ASSERT_TRUE(usb_cdc_port_stats(0u, &stats));
    TEST_ASSERT_FALSE(stats.opened);
    TEST_ASSERT_FALSE(ready[0]);
    TEST_ASSERT_TRUE(stopping[0]);
    TEST_ASSERT_TRUE(quiesced[0]);
}

void test_bus_reset_without_unmount_stops_old_backend(void)
{
    tud_cdc_line_state_cb(0u, true, false);
    ready[0] = true;
    mounted = false;
    usb_cdc_poll();
    TEST_ASSERT_FALSE(ready[0]);
    TEST_ASSERT_TRUE(stopping[0]);
    TEST_ASSERT_TRUE(quiesced[0]);
    uint32_t stopped_generation = generation[0];
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(stopped_generation, generation[0]);
}

void test_initial_settings_gate_rx_until_worker_completion(void)
{
    request_baud(230400u);
    tud_cdc_line_state_cb(0u, true, false);
    usb_cdc_poll();
    TEST_ASSERT_TRUE(request_queued);
    TEST_ASSERT_EQUAL_UINT32(0u, rx_bridge_calls);
    ready[0] = true;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(0u, rx_bridge_calls);
    complete_request(UART_PORT_0);
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, rx_bridge_calls);
}

void test_live_configuration_keeps_rx_draining(void)
{
    open_ready();
    rx_bridge_calls = 0u;
    accept_request = false;
    request_baud(230400u);
    usb_cdc_poll();
    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_CONTROL_PENDING, flags[0]);
    TEST_ASSERT_EQUAL_UINT32(1u, rx_bridge_calls);
}

void test_rejected_initial_replacement_does_not_strand_staged_mailbox(void)
{
    request_baud(115200u);
    cdc_line_coding_t rejected = {.bit_rate = 230400u, .data_bits = 7u};
    tud_cdc_line_coding_cb(0u, &rejected);
    tud_cdc_line_state_cb(0u, true, false);
    usb_cdc_poll();
    TEST_ASSERT_TRUE(request_queued);
    ready[0] = true;
    complete_request(UART_PORT_0);
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(115200u, applied.baud_rate);
    TEST_ASSERT_BITS_HIGH(UART_DRIVER_PORT_STATUS_CONTROL_ERROR, flags[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, rx_bridge_calls);
    request_baud(230400u);
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(230400u, applied.baud_rate);
    TEST_ASSERT_EQUAL_UINT32(1u, rx_bridge_calls);
}

void test_port_flush_deadlines_and_close_are_independent(void)
{
    usb_cdc_port_stats_t stats;
    uint32_t initial_tx_bytes[2];
    for (uint8_t itf = 0u; itf < 2u; ++itf) {
        TEST_ASSERT_TRUE(usb_cdc_port_stats(itf, &stats));
        initial_tx_bytes[itf] = stats.tx_bytes;
    }
    open_ready();
    tud_cdc_line_state_cb(1u, true, false);
    usb_cdc_poll();
    ready[1] = true;
    usb_cdc_poll();
    drain_bytes[0] = 3u;
    usb_cdc_poll();
    pico_test_time_us = 500;
    drain_bytes[1] = 5u;
    usb_cdc_poll();
    pico_test_time_us = 1000;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, flush_calls[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, flush_calls[1]);

    drain_bytes[0] = 2u;
    usb_cdc_poll();
    tud_cdc_line_state_cb(0u, false, false);
    usb_cdc_poll();
    pico_test_time_us = 2000;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, flush_calls[0]);
    TEST_ASSERT_EQUAL_UINT32(1u, flush_calls[1]);
    TEST_ASSERT_TRUE(usb_cdc_port_stats(0u, &stats));
    TEST_ASSERT_FALSE(stats.opened);
    TEST_ASSERT_EQUAL_UINT32(initial_tx_bytes[0] + 5u, stats.tx_bytes);
    TEST_ASSERT_TRUE(usb_cdc_port_stats(1u, &stats));
    TEST_ASSERT_TRUE(stats.opened);
    TEST_ASSERT_EQUAL_UINT32(initial_tx_bytes[1] + 5u, stats.tx_bytes);
    usb_cdc_reset_host_state();
    TEST_ASSERT_TRUE(usb_cdc_port_stats(1u, &stats));
    TEST_ASSERT_FALSE(stats.opened);
    TEST_ASSERT_EQUAL_UINT32(initial_tx_bytes[1] + 5u, stats.tx_bytes);
}

void test_close_flushes_rx_once_while_waiting_for_usb_quiescence(void)
{
    open_ready();
    read_flush_calls[0] = 0u;
    write_clear_ready[0] = false;
    tud_cdc_line_state_cb(0u, false, false);
    usb_cdc_poll();
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, read_flush_calls[0]);
    TEST_ASSERT_FALSE(quiesced[0]);
    write_clear_ready[0] = true;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, read_flush_calls[0]);
    TEST_ASSERT_TRUE(quiesced[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_port_flush_deadlines_and_close_are_independent);
    RUN_TEST(test_close_flushes_rx_once_while_waiting_for_usb_quiescence);
    RUN_TEST(test_rejected_initial_replacement_does_not_strand_staged_mailbox);
    RUN_TEST(test_initial_settings_gate_rx_until_worker_completion);
    RUN_TEST(test_live_configuration_keeps_rx_draining);
    RUN_TEST(test_remount_without_unmount_stops_old_backend);
    RUN_TEST(test_bus_reset_without_unmount_stops_old_backend);
    RUN_TEST(test_unsupported_replacement_does_not_erase_prior_valid_request);
    RUN_TEST(test_closed_configuration_waits_for_open_and_ready);
    RUN_TEST(test_identical_retry_does_not_extend_mailbox_timeout);
    RUN_TEST(test_distinct_replacement_gets_its_own_deadline);
    RUN_TEST(test_close_cancels_old_request_and_preserves_reopened_request);
    RUN_TEST(test_disconnect_cancels_pending_configuration);
    return UNITY_END();
}