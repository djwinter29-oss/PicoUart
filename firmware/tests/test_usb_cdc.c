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

void tusb_init(void) {}
bool tud_disconnect(void) { return true; }
bool tud_connect(void) { return true; }
bool tud_task_event_ready(void) { return false; }
void tud_task(void) {}
uint32_t tud_cdc_n_available(uint8_t itf) { (void)itf; return 0u; }
uint32_t tud_cdc_n_read(uint8_t itf, void *buffer, uint32_t length)
{ (void)itf; (void)buffer; (void)length; return 0u; }
void tud_cdc_n_read_flush(uint8_t itf) { (void)itf; }
uint32_t tud_cdc_n_write(uint8_t itf, const void *buffer, uint32_t length)
{ (void)itf; (void)buffer; (void)length; return 0u; }
uint32_t tud_cdc_n_write_available(uint8_t itf) { (void)itf; return 0u; }
uint32_t tud_cdc_n_write_flush(uint8_t itf) { (void)itf; return 0u; }
bool tud_cdc_n_write_clear(uint8_t itf) { (void)itf; return true; }
void service_load_note(unsigned int core) { TEST_ASSERT_EQUAL_UINT(0u, core); }
void usb_hid_reset_host_state(void) {}

bool uart_driver_port_is_ready(uart_port_id_t port) { return ready[port]; }
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
bool uart_driver_queue_line_coding(uart_port_id_t port, const uart_driver_line_coding_t *coding,
                                   uint32_t request_generation)
{
    if (!accept_request) return false;
    applied = *coding;
    ++applied_count;
    if (request_generation == generation[port]) flags[port] = 0u;
    return true;
}
size_t uart_driver_fill_tx(uart_port_id_t port, size_t capacity,
                           uint32_t (*reader)(void *, uint8_t *, uint32_t), void *context)
{ (void)port; (void)capacity; (void)reader; (void)context; return 0u; }
size_t uart_driver_drain_rx(uart_port_id_t port, size_t capacity,
                            uint32_t (*writer)(void *, const uint8_t *, uint32_t), void *context)
{ (void)port; (void)capacity; (void)writer; (void)context; return 0u; }
size_t uart_driver_recover_rx(uart_port_id_t port) { (void)port; return 0u; }

static void request_baud(uint32_t baud)
{
    cdc_line_coding_t coding = {.bit_rate = baud, .data_bits = 8u};
    tud_cdc_line_coding_cb(0u, &coding);
}

void setUp(void)
{
    memset(ready, 0, sizeof(ready));
    memset(stopping, 0, sizeof(stopping));
    memset(quiesced, 0, sizeof(quiesced));
    memset(flags, 0, sizeof(flags));
    memset(generation, 0, sizeof(generation));
    applied_count = 0u;
    applied = (uart_driver_line_coding_t){0};
    accept_request = true;
    pico_test_time_us = 0;
    usb_cdc_reset_host_state();
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
    usb_cdc_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, applied_count);
    TEST_ASSERT_EQUAL_UINT32(230400u, applied.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(0u, flags[0]);
}

void test_identical_retry_does_not_extend_mailbox_timeout(void)
{
    tud_cdc_line_state_cb(0u, true, false);
    ready[0] = true;
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
    tud_cdc_line_state_cb(0u, true, false);
    ready[0] = true;
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
    tud_cdc_line_state_cb(0u, true, false);
    ready[0] = true;
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
    TEST_ASSERT_EQUAL_UINT32(1u, applied_count);
    TEST_ASSERT_EQUAL_UINT32(230400u, applied.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(0u, flags[0]);
}

void test_disconnect_cancels_pending_configuration(void)
{
    tud_cdc_line_state_cb(0u, true, false);
    ready[0] = true;
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_closed_configuration_waits_for_open_and_ready);
    RUN_TEST(test_identical_retry_does_not_extend_mailbox_timeout);
    RUN_TEST(test_distinct_replacement_gets_its_own_deadline);
    RUN_TEST(test_close_cancels_old_request_and_preserves_reopened_request);
    RUN_TEST(test_disconnect_cancels_pending_configuration);
    return UNITY_END();
}