/**
 * @file test_usb_cdc.c
 * @brief Exercise production CDC and ring bridging with simulated USB FIFOs, ownership, and time.
 */

#include "unity.h"
#include "tusb.h"
#include "pico/time.h"
#include "config/capacity_config.h"
#include "uart/bridge.h"
#include "uart/uart_driver.h"
#include "usb/usb_cdc.h"
#include <string.h>

absolute_time_t pico_test_time_us;
static bool ready[UART_PORT_COUNT];
static uint32_t readiness_queries;
static bool stopping[UART_PORT_COUNT];
static bool quiesced[UART_PORT_COUNT];
static uint8_t flags[UART_PORT_COUNT];
static uint32_t generation[UART_PORT_COUNT];
static uart_driver_control_error_t last_rejection;
static bool reject_baud;
static bool accept_request;
static uint32_t applied_count;
static uart_driver_line_coding_t applied;
static bool mounted;
static bool request_queued[UART_PORT_COUNT];
static bool mailbox_blocked[UART_PORT_COUNT];
static uint32_t queued_generation[UART_PORT_COUNT];
static uart_driver_line_coding_t queued_coding[UART_PORT_COUNT];
static uint32_t rx_bridge_calls;
static uint32_t flush_calls[UART_PORT_COUNT];
static uint32_t read_flush_calls[UART_PORT_COUNT];
static bool write_clear_ready[UART_PORT_COUNT];
static ring_buffer_t uart_tx[UART_PORT_COUNT];
static ring_buffer_t uart_rx[UART_PORT_COUNT];
static ring_buffer_t usb_out[UART_PORT_COUNT];
static ring_buffer_t usb_in[UART_PORT_COUNT];
static uint8_t uart_tx_storage[UART_PORT_COUNT][PICO_UART_HW_UART_TX_BUFFER_SIZE];
static uint8_t uart_rx_storage[UART_PORT_COUNT][PICO_UART_HW_UART_RX_BUFFER_SIZE];
static uint8_t usb_out_storage[UART_PORT_COUNT][PICO_UART_USB_CDC_RX_BUFFER_SIZE];
static uint8_t usb_in_storage[UART_PORT_COUNT][PICO_UART_USB_CDC_TX_BUFFER_SIZE];
static uint32_t write_limit[UART_PORT_COUNT];
static volatile uint32_t stats_sequence[UART_PORT_COUNT];
static uint32_t recovery_calls[UART_PORT_COUNT];
static uint8_t endpoint_data[UART_PORT_COUNT][PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE];
static size_t endpoint_length[UART_PORT_COUNT];

void tusb_init(void) {}
bool tud_disconnect(void) { return true; }
bool tud_connect(void) { return true; }
bool tud_task_event_ready(void) { return false; }
void tud_task(void) {}
bool tud_mounted(void) { return mounted; }
uint32_t tud_cdc_n_available(uint8_t itf) { return (uint32_t)ring_buffer_occupancy(&usb_out[itf]); }
uint32_t tud_cdc_n_read(uint8_t itf, void *buffer, uint32_t length)
{ return (uint32_t)ring_buffer_read(&usb_out[itf], buffer, length); }
void tud_cdc_n_read_flush(uint8_t itf)
{
    ++read_flush_calls[itf];
    ring_buffer_clear(&usb_out[itf]);
}
uint32_t tud_cdc_n_write(uint8_t itf, const void *buffer, uint32_t length)
{
    if (length > write_limit[itf]) length = write_limit[itf];
    uint32_t written = (uint32_t)ring_buffer_write(&usb_in[itf], buffer, length);
    if (ring_buffer_occupancy(&usb_in[itf]) >= PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE) {
        (void)tud_cdc_n_write_flush(itf);
    }
    return written;
}
uint32_t tud_cdc_n_write_available(uint8_t itf) { return (uint32_t)ring_buffer_free_space(&usb_in[itf]); }
uint32_t tud_cdc_n_write_flush(uint8_t itf)
{
    ++flush_calls[itf];
    if (endpoint_length[itf] != 0u) return 0u;
    endpoint_length[itf] = ring_buffer_read(&usb_in[itf], endpoint_data[itf], sizeof(endpoint_data[itf]));
    return (uint32_t)endpoint_length[itf];
}
bool tud_cdc_n_write_clear(uint8_t itf)
{
    if (!write_clear_ready[itf]) return false;
    ring_buffer_clear(&usb_in[itf]);
    return true;
}
void service_load_note(unsigned int core) { TEST_ASSERT_EQUAL_UINT(0u, core); }
void usb_hid_reset_host_state(void) {}

bool uart_driver_port_is_ready(uart_port_id_t port) { ++readiness_queries; return ready[port]; }
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
    request_queued[port] = false;
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
void uart_driver_report_control_error(uart_port_id_t port, uart_driver_control_error_t reason)
{
    TEST_ASSERT_NOT_EQUAL(UART_CONTROL_ERROR_NONE, reason);
    last_rejection = reason;
    ++generation[port];
    flags[port] |= UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
}
void uart_driver_report_soft_pending_error(uart_port_id_t port, uint32_t request_generation,
                                           uart_driver_control_error_t reason)
{
    TEST_ASSERT_NOT_EQUAL(UART_CONTROL_ERROR_NONE, reason);
    if (request_generation == generation[port]) {
        flags[port] = UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    }
}
bool uart_driver_line_coding_acceptable(uart_port_id_t port, const uart_driver_line_coding_t *coding)
{ (void)port; return !reject_baud && coding != NULL && coding->data_bits == 8u; }
bool uart_driver_port_info(uart_port_id_t port, uart_driver_port_info_t *info)
{
    *info = (uart_driver_port_info_t){.backend = port < UART_PORT_2 ?
        UART_DRIVER_BACKEND_HW : UART_DRIVER_BACKEND_PIO};
    return true;
}

static void complete_request(uart_port_id_t port)
{
    if (!request_queued[port]) return;
    applied = queued_coding[port];
    ++applied_count;
    request_queued[port] = false;
    flags[port] &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    if (queued_generation[port] == generation[port]) flags[port] = 0u;
}

bool uart_driver_queue_line_coding(uart_port_id_t port, const uart_driver_line_coding_t *coding,
                                   uint32_t request_generation)
{
    if (!accept_request || mailbox_blocked[port] || request_queued[port]) return false;
    queued_coding[port] = *coding;
    queued_generation[port] = request_generation;
    request_queued[port] = true;
    if (ready[port]) complete_request(port);
    return true;
}
size_t uart_driver_fill_tx(uart_port_id_t port, size_t capacity,
                           uint32_t (*reader)(void *, uint8_t *, uint32_t), void *context)
{
    if (flags[port] & UART_DRIVER_PORT_STATUS_CONTROL_PENDING) return 0u;
    return uart_bridge_fill_tx(&uart_tx[port], capacity, reader, context);
}

static bool snapshot_is_current(void *context, uint32_t consumer_sequence)
{
    (void)context;
    (void)consumer_sequence;
    return true;
}

size_t uart_driver_drain_rx(uart_port_id_t port, size_t capacity,
                            uint32_t (*writer)(void *, const uint8_t *, uint32_t), void *context,
                            uint8_t *snapshot, size_t snapshot_capacity)
{
    ++rx_bridge_calls;
    return uart_bridge_drain_rx(&uart_rx[port], capacity, writer, context, snapshot, snapshot_capacity,
                                snapshot_is_current, NULL, &stats_sequence[port]);
}
size_t uart_driver_recover_rx(uart_port_id_t port)
{
    ++recovery_calls[port];
    return uart_bridge_recover_rx(&uart_rx[port]);
}

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
    readiness_queries = 0u;
    memset(stopping, 0, sizeof(stopping));
    memset(quiesced, 0, sizeof(quiesced));
    memset(flags, 0, sizeof(flags));
    memset(generation, 0, sizeof(generation));
    last_rejection = UART_CONTROL_ERROR_NONE;
    reject_baud = false;
    applied_count = 0u;
    applied = (uart_driver_line_coding_t){0};
    accept_request = true;
    memset(request_queued, 0, sizeof(request_queued));
    memset(mailbox_blocked, 0, sizeof(mailbox_blocked));
    memset(queued_generation, 0, sizeof(queued_generation));
    memset(queued_coding, 0, sizeof(queued_coding));
    rx_bridge_calls = 0u;
    memset(recovery_calls, 0, sizeof(recovery_calls));
    memset(endpoint_length, 0, sizeof(endpoint_length));
    memset(flush_calls, 0, sizeof(flush_calls));
    memset(read_flush_calls, 0, sizeof(read_flush_calls));
    for (size_t port = 0u; port < UART_PORT_COUNT; ++port) {
        write_clear_ready[port] = true;
        write_limit[port] = UINT32_MAX;
        stats_sequence[port] = 0u;
        ring_buffer_init(&uart_tx[port], uart_tx_storage[port], sizeof(uart_tx_storage[port]));
        ring_buffer_init(&uart_rx[port], uart_rx_storage[port], sizeof(uart_rx_storage[port]));
        ring_buffer_init(&usb_out[port], usb_out_storage[port], sizeof(usb_out_storage[port]));
        ring_buffer_init(&usb_in[port], usb_in_storage[port], sizeof(usb_in_storage[port]));
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
    TEST_ASSERT_TRUE(request_queued[0]);
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
    TEST_ASSERT_TRUE(request_queued[0]);
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
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_write(&uart_rx[0], (const uint8_t *)"abc", 3u));
    usb_cdc_poll();
    pico_test_time_us = 500;
    TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&uart_rx[1], (const uint8_t *)"12345", 5u));
    usb_cdc_poll();
    pico_test_time_us = 1000;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, flush_calls[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, flush_calls[1]);

    TEST_ASSERT_EQUAL_UINT(2u, ring_buffer_write(&uart_rx[0], (const uint8_t *)"de", 2u));
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

static void open_all_ready(void)
{
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        tud_cdc_line_state_cb(itf, true, false);
    }
    usb_cdc_poll();
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        ready[itf] = true;
    }
    usb_cdc_poll();
}

void test_closed_ports_skip_empty_flushes_but_discard_repeated_late_out_data(void)
{
    uint8_t actual[5];
    for (size_t pass = 0u; pass < 12u; ++pass) usb_cdc_poll();
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        TEST_ASSERT_EQUAL_UINT32(0u, read_flush_calls[itf]);
    }
    for (uint32_t round = 0u; round < 3u; ++round) {
        for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
            TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&usb_out[itf], (const uint8_t *)"stale", 5u));
        }
        usb_cdc_poll();
        usb_cdc_poll();
        for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
            TEST_ASSERT_EQUAL_UINT32(round + 1u, read_flush_calls[itf]);
            TEST_ASSERT_EQUAL_UINT(0u, tud_cdc_n_available(itf));
            TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&uart_tx[itf]));
        }
    }
    open_all_ready();
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&usb_out[itf], (const uint8_t *)"fresh", 5u));
    }
    usb_cdc_poll();
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        TEST_ASSERT_EQUAL_UINT32(3u, read_flush_calls[itf]);
        TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_read(&uart_tx[itf], actual, sizeof(actual)));
        TEST_ASSERT_EQUAL_MEMORY("fresh", actual, sizeof(actual));
    }
}

void test_six_ports_preserve_bytes_through_repeated_pressure_and_ring_wrap(void)
{
    uint8_t outgoing[UART_PORT_COUNT][773];
    uint8_t incoming[UART_PORT_COUNT][773];
    uint8_t actual[PICO_UART_HW_UART_TX_BUFFER_SIZE];
    uint8_t filler[sizeof(actual)];
    usb_cdc_port_stats_t before[UART_PORT_COUNT], after;
    const size_t length = sizeof(outgoing[0]);
    memset(filler, 0xa5, sizeof(filler));
    open_all_ready();
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        TEST_ASSERT_TRUE(usb_cdc_port_stats(itf, &before[itf]));
    }

    for (uint32_t round = 0u; round < 32u; ++round) {
        size_t tx_received[UART_PORT_COUNT] = {0};
        size_t rx_received[UART_PORT_COUNT] = {0};
        uint8_t blocked_tx = (uint8_t)(round % UART_PORT_COUNT);
        uint8_t blocked_rx = (uint8_t)((blocked_tx + 1u) % UART_PORT_COUNT);
        TEST_ASSERT_EQUAL_UINT(sizeof(filler),
                               ring_buffer_write(&uart_tx[blocked_tx], filler, sizeof(filler)));
        for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
            for (size_t index = 0u; index < length; ++index) {
                outgoing[itf][index] = (uint8_t)(index * 13u + index / 251u + round * 7u + itf * 31u);
                incoming[itf][index] = (uint8_t)~outgoing[itf][index];
            }
            TEST_ASSERT_EQUAL_UINT(length, ring_buffer_write(&usb_out[itf], outgoing[itf], length));
            TEST_ASSERT_EQUAL_UINT(length, ring_buffer_write(&uart_rx[itf], incoming[itf], length));
            write_limit[itf] = itf == blocked_rx ? 0u : 1u + itf * 7u;
        }
        usb_cdc_poll();
        TEST_ASSERT_EQUAL_UINT(length, tud_cdc_n_available(blocked_tx));
        TEST_ASSERT_EQUAL_UINT(length, ring_buffer_occupancy(&uart_rx[blocked_rx]));
        TEST_ASSERT_EQUAL_UINT(sizeof(filler), ring_buffer_read(&uart_tx[blocked_tx], actual, sizeof(actual)));
        TEST_ASSERT_EQUAL_MEMORY(filler, actual, sizeof(filler));

        bool complete = false;
        for (uint32_t pass = 0u; pass < 128u; ++pass) {
            pico_test_time_us += 1000;
            for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
                write_limit[itf] = (round + pass + itf) % 4u == 0u ? 0u : 17u + itf * 13u;
            }
            usb_cdc_poll();
            complete = true;
            for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
                size_t count = ring_buffer_read(&uart_tx[itf], actual, sizeof(actual));
                TEST_ASSERT_LESS_OR_EQUAL_UINT(length - tx_received[itf], count);
                if (count != 0u) {
                    TEST_ASSERT_EQUAL_MEMORY(outgoing[itf] + tx_received[itf], actual, count);
                    tx_received[itf] += count;
                }
                count = endpoint_length[itf];
                TEST_ASSERT_LESS_OR_EQUAL_UINT(length - rx_received[itf], count);
                if (count != 0u) {
                    TEST_ASSERT_EQUAL_MEMORY(incoming[itf] + rx_received[itf], endpoint_data[itf], count);
                    rx_received[itf] += count;
                    endpoint_length[itf] = 0u;
                    /* Model TinyUSB's completion-driven flush, including queued partial packets. */
                    (void)tud_cdc_n_write_flush(itf);
                }
                complete = complete && tx_received[itf] == length && rx_received[itf] == length;
            }
            if (complete) break;
        }
        TEST_ASSERT_TRUE_MESSAGE(complete, "Every stream must finish within the bounded recovery passes");
        for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
            TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&uart_tx[itf]));
            TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&uart_rx[itf]));
            TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&usb_out[itf]));
            TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&usb_in[itf]));
            TEST_ASSERT_EQUAL_UINT(0u, endpoint_length[itf]);
            TEST_ASSERT_TRUE(usb_cdc_port_stats(itf, &after));
            TEST_ASSERT_EQUAL_UINT32(before[itf].rx_bytes + (round + 1u) * length, after.rx_bytes);
            TEST_ASSERT_EQUAL_UINT32(before[itf].tx_bytes + (round + 1u) * length, after.tx_bytes);
            TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_overflow_count(&uart_rx[itf]));
        }
    }
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        TEST_ASSERT_GREATER_THAN_UINT32(4u * sizeof(uart_tx_storage[itf]), uart_tx[itf].producer);
        TEST_ASSERT_GREATER_THAN_UINT32(4u * sizeof(uart_rx_storage[itf]), uart_rx[itf].producer);
        TEST_ASSERT_GREATER_THAN_UINT32(8u * sizeof(usb_out_storage[itf]), usb_out[itf].producer);
        TEST_ASSERT_GREATER_THAN_UINT32(8u * sizeof(usb_in_storage[itf]), usb_in[itf].producer);
    }
}

void test_repeated_close_reopen_discards_queued_data_without_stalling_other_ports(void)
{
    uint8_t actual[5];
    usb_cdc_port_stats_t before[UART_PORT_COUNT], after;
    open_all_ready();
    for (uint32_t round = 0u; round < 12u; ++round) {
        uint8_t closing = (uint8_t)(round % UART_PORT_COUNT);
        for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
            TEST_ASSERT_TRUE(usb_cdc_port_stats(itf, &before[itf]));
            TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&usb_out[itf], (const uint8_t *)"fresh", 5u));
            TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&uart_rx[itf], (const uint8_t *)"reply", 5u));
        }
        TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&usb_in[closing], (const uint8_t *)"stale", 5u));
        tud_cdc_line_state_cb(closing, false, false);
        usb_cdc_poll();
        TEST_ASSERT_TRUE(quiesced[closing]);
        TEST_ASSERT_EQUAL_UINT(0u, tud_cdc_n_available(closing));
        TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&usb_in[closing]));

        /* Complete worker-owned stop; the mock does not execute backend lifecycle code. */
        ring_buffer_clear(&uart_tx[closing]);
        ring_buffer_clear(&uart_rx[closing]);
        stopping[closing] = quiesced[closing] = false;
        TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&usb_out[closing], (const uint8_t *)"late!", 5u));
        usb_cdc_poll();
        TEST_ASSERT_EQUAL_UINT(0u, tud_cdc_n_available(closing));
        tud_cdc_line_state_cb(closing, true, false);
        usb_cdc_poll();
        ready[closing] = true;
        usb_cdc_poll();
        TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&usb_out[closing], (const uint8_t *)"fresh", 5u));
        TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&uart_rx[closing], (const uint8_t *)"reply", 5u));
        usb_cdc_poll();
        pico_test_time_us += 1000;
        usb_cdc_poll();
        for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
            TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_read(&uart_tx[itf], actual, sizeof(actual)));
            TEST_ASSERT_EQUAL_MEMORY("fresh", actual, sizeof(actual));
            TEST_ASSERT_EQUAL_UINT(5u, endpoint_length[itf]);
            TEST_ASSERT_EQUAL_MEMORY("reply", endpoint_data[itf], 5u);
            endpoint_length[itf] = 0u;
            TEST_ASSERT_EQUAL_UINT(0u, tud_cdc_n_write_flush(itf));
            TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&uart_rx[itf]));
            TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&uart_tx[itf]));
            TEST_ASSERT_TRUE(usb_cdc_port_stats(itf, &after));
            TEST_ASSERT_TRUE(after.opened);
            TEST_ASSERT_EQUAL_UINT32(before[itf].rx_bytes + 5u, after.rx_bytes);
            TEST_ASSERT_EQUAL_UINT32(before[itf].tx_bytes + 5u, after.tx_bytes);
        }
    }
}

void test_full_uart_tx_preserves_usb_out_until_space_returns(void)
{
    uint8_t payload[PICO_UART_HW_UART_TX_BUFFER_SIZE];
    uint8_t actual[sizeof(payload)];
    usb_cdc_port_stats_t before, after;
    memset(payload, 0xa5, sizeof(payload));
    open_ready();
    TEST_ASSERT_TRUE(usb_cdc_port_stats(0u, &before));
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), ring_buffer_write(&uart_tx[0], payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT(137u, ring_buffer_write(&usb_out[0], payload, 137u));
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT(137u, tud_cdc_n_available(0u));
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), ring_buffer_read(&uart_tx[0], actual, sizeof(actual)));
    TEST_ASSERT_EQUAL_MEMORY(payload, actual, sizeof(payload));
    TEST_ASSERT_TRUE(usb_cdc_port_stats(0u, &after));
    TEST_ASSERT_EQUAL_UINT32(before.rx_bytes, after.rx_bytes);

    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tud_cdc_n_available(0u));
    TEST_ASSERT_EQUAL_UINT(137u, ring_buffer_read(&uart_tx[0], actual, sizeof(actual)));
    TEST_ASSERT_EQUAL_MEMORY(payload, actual, 137u);
    TEST_ASSERT_TRUE(usb_cdc_port_stats(0u, &after));
    TEST_ASSERT_EQUAL_UINT32(before.rx_bytes + 137u, after.rx_bytes);
}

void test_full_usb_in_recovers_overwrites_without_blocking_another_port(void)
{
    uint8_t payload[PICO_UART_USB_CDC_TX_BUFFER_SIZE] = {0};
    open_all_ready();
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), ring_buffer_write(&usb_in[0], payload, sizeof(payload)));
    ring_buffer_produce_external(&uart_rx[0], sizeof(uart_rx_storage[0]) + 17u);
    TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&uart_rx[1], (const uint8_t *)"hello", 5u));
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_write(&usb_out[1], (const uint8_t *)"out", 3u));
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, recovery_calls[0]);
    TEST_ASSERT_EQUAL_UINT(17u, ring_buffer_overflow_count(&uart_rx[0]));
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_pending_overflow(&uart_rx[0]));
    TEST_ASSERT_EQUAL_UINT(sizeof(uart_rx_storage[0]), ring_buffer_occupancy(&uart_rx[0]));
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&uart_rx[1]));
    TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_occupancy(&usb_in[1]));
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_occupancy(&uart_tx[1]));
    TEST_ASSERT_EQUAL_MEMORY("hello", usb_in_storage[1], 5u);
    TEST_ASSERT_EQUAL_MEMORY("out", uart_tx_storage[1], 3u);
}

void test_partial_and_zero_usb_acceptance_retire_only_accepted_rx(void)
{
    usb_cdc_port_stats_t before, after;
    open_ready();
    TEST_ASSERT_TRUE(usb_cdc_port_stats(0u, &before));
    TEST_ASSERT_EQUAL_UINT(10u, ring_buffer_write(&uart_rx[0], (const uint8_t *)"0123456789", 10u));
    write_limit[0] = 3u;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT(7u, ring_buffer_occupancy(&uart_rx[0]));
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_occupancy(&usb_in[0]));
    TEST_ASSERT_TRUE(usb_cdc_port_stats(0u, &after));
    TEST_ASSERT_EQUAL_UINT32(before.tx_bytes + 3u, after.tx_bytes);
    write_limit[0] = 0u;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT(7u, ring_buffer_occupancy(&uart_rx[0]));
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_occupancy(&usb_in[0]));
    TEST_ASSERT_TRUE(usb_cdc_port_stats(0u, &after));
    TEST_ASSERT_EQUAL_UINT32(before.tx_bytes + 3u, after.tx_bytes);
    write_limit[0] = UINT32_MAX;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&uart_rx[0]));
    TEST_ASSERT_EQUAL_UINT(10u, ring_buffer_occupancy(&usb_in[0]));
    TEST_ASSERT_EQUAL_MEMORY("0123456789", usb_in_storage[0], 10u);
    TEST_ASSERT_TRUE(usb_cdc_port_stats(0u, &after));
    TEST_ASSERT_EQUAL_UINT32(before.tx_bytes + 10u, after.tx_bytes);
}

void test_six_ports_obey_each_direction_budget_under_different_pressure(void)
{
    uint8_t payload[PICO_UART_USB_CDC_RX_BUFFER_SIZE];
    uint8_t actual[sizeof(payload)];
    usb_cdc_port_stats_t before[UART_PORT_COUNT], after;
    const size_t budget = PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET;
    open_all_ready();
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        for (size_t index = 0u; index < sizeof(payload); ++index) payload[index] = (uint8_t)(index + itf);
        TEST_ASSERT_TRUE(usb_cdc_port_stats(itf, &before[itf]));
        TEST_ASSERT_EQUAL_UINT(sizeof(payload), ring_buffer_write(&usb_out[itf], payload, sizeof(payload)));
        TEST_ASSERT_EQUAL_UINT(sizeof(payload), ring_buffer_write(&uart_rx[itf], payload, sizeof(payload)));
        /* Leave a different amount of TX space on each port, including one full ring. */
        size_t occupied = itf == 0u ? sizeof(uart_tx_storage[itf]) : sizeof(uart_tx_storage[itf]) - itf * 100u;
        ring_buffer_produce_external(&uart_tx[itf], (uint32_t)occupied);
        if (itf == 1u) {
            TEST_ASSERT_EQUAL_UINT(sizeof(payload), ring_buffer_write(&usb_in[itf], payload, sizeof(payload)));
        } else if (itf == 2u) {
            write_limit[itf] = 19u;
        }
    }
    usb_cdc_poll();
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        size_t expected_out = itf * 100u;
        if (expected_out > budget) expected_out = budget;
        size_t expected_in = itf == 1u ? 0u : (itf == 2u ? 19u : budget);
        TEST_ASSERT_EQUAL_UINT(sizeof(payload) - expected_out, ring_buffer_occupancy(&usb_out[itf]));
        TEST_ASSERT_EQUAL_UINT(sizeof(payload) - expected_in, ring_buffer_occupancy(&uart_rx[itf]));
        TEST_ASSERT_TRUE(usb_cdc_port_stats(itf, &after));
        TEST_ASSERT_EQUAL_UINT32(before[itf].rx_bytes + expected_out, after.rx_bytes);
        TEST_ASSERT_EQUAL_UINT32(before[itf].tx_bytes + expected_in, after.tx_bytes);
    }

    /* Remove pressure and verify a full budget in both directions, not just an upper bound. */
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        ring_buffer_clear(&uart_tx[itf]);
        ring_buffer_clear(&usb_in[itf]);
        endpoint_length[itf] = 0u;
        write_limit[itf] = UINT32_MAX;
        TEST_ASSERT_TRUE(usb_cdc_port_stats(itf, &before[itf]));
    }
    usb_cdc_poll();
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        TEST_ASSERT_TRUE(usb_cdc_port_stats(itf, &after));
        TEST_ASSERT_EQUAL_UINT32(before[itf].rx_bytes + budget, after.rx_bytes);
        TEST_ASSERT_EQUAL_UINT32(before[itf].tx_bytes + budget, after.tx_bytes);
        TEST_ASSERT_EQUAL_UINT(budget, ring_buffer_read(&uart_tx[itf], actual, sizeof(actual)));
        size_t offset = itf * 100u;
        if (offset > budget) offset = budget;
        for (size_t index = 0u; index < budget; ++index) {
            TEST_ASSERT_EQUAL_UINT8((uint8_t)(offset + index + itf), actual[index]);
        }
        TEST_ASSERT_EQUAL_UINT(PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE, endpoint_length[itf]);
        memcpy(actual, endpoint_data[itf], endpoint_length[itf]);
        TEST_ASSERT_EQUAL_UINT(budget - endpoint_length[itf],
                               ring_buffer_read(&usb_in[itf], actual + endpoint_length[itf],
                                                sizeof(actual) - endpoint_length[itf]));
        offset = itf == 1u ? 0u : (itf == 2u ? 19u : budget);
        for (size_t index = 0u; index < budget; ++index) {
            TEST_ASSERT_EQUAL_UINT8((uint8_t)(offset + index + itf), actual[index]);
        }
    }
}

void test_busy_in_endpoint_drains_partial_fifo_on_completion_without_new_uart_data(void)
{
    open_ready();
    TEST_ASSERT_EQUAL_UINT(3u, ring_buffer_write(&uart_rx[0], (const uint8_t *)"abc", 3u));
    usb_cdc_poll();
    pico_test_time_us = 1000;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT(3u, endpoint_length[0]);
    TEST_ASSERT_EQUAL_MEMORY("abc", endpoint_data[0], 3u);

    TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_write(&uart_rx[0], (const uint8_t *)"12345", 5u));
    usb_cdc_poll();
    pico_test_time_us = 2000;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(2u, flush_calls[0]);
    TEST_ASSERT_EQUAL_UINT(5u, ring_buffer_occupancy(&usb_in[0]));
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&uart_rx[0]));

    /* TinyUSB cdcd_xfer_cb flushes the FIFO after the occupied IN transfer completes. */
    endpoint_length[0] = 0u;
    TEST_ASSERT_EQUAL_UINT32(5u, tud_cdc_n_write_flush(0u));
    TEST_ASSERT_EQUAL_MEMORY("12345", endpoint_data[0], 5u);
    TEST_ASSERT_EQUAL_UINT(0u, ring_buffer_occupancy(&usb_in[0]));
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(3u, flush_calls[0]);
}

void test_simultaneous_initial_requests_keep_independent_mailboxes(void)
{
    mailbox_blocked[0] = true;
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        cdc_line_coding_t coding = {.bit_rate = 115200u * (itf + 1u), .data_bits = 8u};
        tud_cdc_line_coding_cb(itf, &coding);
        tud_cdc_line_state_cb(itf, true, false);
    }
    usb_cdc_poll();
    TEST_ASSERT_FALSE(request_queued[0]);
    for (uint8_t itf = 1u; itf < UART_PORT_COUNT; ++itf) {
        TEST_ASSERT_TRUE(request_queued[itf]);
        TEST_ASSERT_EQUAL_UINT32(115200u * (itf + 1u), queued_coding[itf].baud_rate);
    }
    mailbox_blocked[0] = false;
    usb_cdc_poll();
    TEST_ASSERT_TRUE(request_queued[0]);
    /* Complete in reverse order to catch shared mailbox state. */
    for (size_t count = UART_PORT_COUNT; count != 0u; --count) {
        uart_port_id_t port = (uart_port_id_t)(count - 1u);
        ready[port] = true;
        complete_request(port);
        TEST_ASSERT_EQUAL_UINT32(115200u * count, applied.baud_rate);
        TEST_ASSERT_EQUAL_UINT8(0u, flags[port]);
    }
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(UART_PORT_COUNT, applied_count);
    for (uint8_t itf = 0u; itf < UART_PORT_COUNT; ++itf) {
        TEST_ASSERT_FALSE(request_queued[itf]);
        TEST_ASSERT_EQUAL_UINT8(0u, flags[itf]);
    }
}

void test_backend_rejection_distinguishes_hardware_framing_from_pio_limit(void)
{
    cdc_line_coding_t coding = {.bit_rate = 50u, .data_bits = 7u, .parity = 2u};
    reject_baud = true;
    tud_cdc_line_coding_cb(0u, &coding);
    TEST_ASSERT_EQUAL_UINT(UART_CONTROL_ERROR_BAUD, last_rejection);
    tud_cdc_line_coding_cb(2u, &coding);
    TEST_ASSERT_EQUAL_UINT(UART_CONTROL_ERROR_FRAMING, last_rejection);
    coding.data_bits = 8u;
    coding.parity = 0u;
    tud_cdc_line_coding_cb(2u, &coding);
    TEST_ASSERT_EQUAL_UINT(UART_CONTROL_ERROR_BAUD, last_rejection);
    coding.stop_bits = 1u;
    tud_cdc_line_coding_cb(0u, &coding);
    TEST_ASSERT_EQUAL_UINT(UART_CONTROL_ERROR_FRAMING, last_rejection);
}

void test_live_pending_staging_reuses_poll_readiness_check(void)
{
    open_ready();
    request_baud(230400u);
    readiness_queries = 0u;
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, readiness_queries);
    TEST_ASSERT_EQUAL_UINT32(230400u, applied.baud_rate);
    TEST_ASSERT_BITS_LOW(UART_DRIVER_PORT_STATUS_CONTROL_PENDING, flags[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_backend_rejection_distinguishes_hardware_framing_from_pio_limit);
    RUN_TEST(test_live_pending_staging_reuses_poll_readiness_check);
    RUN_TEST(test_closed_ports_skip_empty_flushes_but_discard_repeated_late_out_data);
    RUN_TEST(test_six_ports_preserve_bytes_through_repeated_pressure_and_ring_wrap);
    RUN_TEST(test_repeated_close_reopen_discards_queued_data_without_stalling_other_ports);
    RUN_TEST(test_full_uart_tx_preserves_usb_out_until_space_returns);
    RUN_TEST(test_full_usb_in_recovers_overwrites_without_blocking_another_port);
    RUN_TEST(test_partial_and_zero_usb_acceptance_retire_only_accepted_rx);
    RUN_TEST(test_six_ports_obey_each_direction_budget_under_different_pressure);
    RUN_TEST(test_busy_in_endpoint_drains_partial_fifo_on_completion_without_new_uart_data);
    RUN_TEST(test_simultaneous_initial_requests_keep_independent_mailboxes);
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