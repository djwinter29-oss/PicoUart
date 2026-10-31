// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file test_usb_hid.c
 * @brief Exercise production HID payloads, acquisition/accounting, and pre-ACK transfer handling.
 */
#include "unity.h"
#if defined(__APPLE__)
/* Same map as the ELF .set symbols below. arm64 Mach-O rejects absolute-symbol relocations. */
#define PICO_UART_LINK_ADDR(symbol) (pico_uart_host_link_##symbol)
#define pico_uart_host_link___end__ (0x20001234u)
#define pico_uart_host_link___scratch_x_start__ (0x20040000u)
#define pico_uart_host_link___scratch_x_end__ (0x20040020u)
#define pico_uart_host_link___scratch_y_start__ (0x20041000u)
#define pico_uart_host_link___scratch_y_end__ (0x20041040u)
#define pico_uart_host_link___StackBottom (0x20041800u)
#define pico_uart_host_link___StackTop (0x20042000u)
#define pico_uart_host_link___StackOneBottom (0x20040800u)
#define pico_uart_host_link___StackOneTop (0x20041000u)
#else
#define __end__ pico_uart_test_static_end
#endif
#include "../src/usb/usb_hid.c"
#if !defined(__APPLE__)
#undef __end__

__asm__(
    ".global pico_uart_test_static_end\n"
    ".global __scratch_x_start__, __scratch_x_end__, __scratch_y_start__, __scratch_y_end__\n"
    ".global __StackBottom, __StackTop, __StackOneBottom, __StackOneTop\n"
    ".set pico_uart_test_static_end, 0x20001234\n"
    ".set __scratch_x_start__, 0x20040000\n"
    ".set __scratch_x_end__, 0x20040020\n"
    ".set __scratch_y_start__, 0x20041000\n"
    ".set __scratch_y_end__, 0x20041040\n"
    ".set __StackBottom, 0x20041800\n"
    ".set __StackTop, 0x20042000\n"
    ".set __StackOneBottom, 0x20040800\n"
    ".set __StackOneTop, 0x20041000\n"
);
#else
_Static_assert((uint32_t)((pico_uart_host_link___end__ - SRAM_BASE) +
                          (pico_uart_host_link___scratch_x_end__ - pico_uart_host_link___scratch_x_start__) +
                          (pico_uart_host_link___scratch_y_end__ - pico_uart_host_link___scratch_y_start__) +
                          (pico_uart_host_link___StackTop - pico_uart_host_link___StackBottom) +
                          (pico_uart_host_link___StackOneTop - pico_uart_host_link___StackOneBottom)) == 0x2294u,
               "Mach-O link map must match the identity reserved-RAM fixture");
#endif

absolute_time_t pico_test_time_us;
static bool led_available, led_on, cdc_open, endpoint_ready, endpoint_accepts;
static bool stats_available, snapshot_available, config_set_succeeds;
static float temperature;
static uint32_t clock_hz;
static uint8_t accepted_report_id, accepted_payload[USB_HID_CHANNEL_SIZE];
static unsigned accepted_reports, reset_calls, save_calls, delegated_transfers;
static boot_config_save_result_t config_save_result;
static boot_config_t staged_config;
static boot_config_status_t boot_status;
static uart_driver_port_info_t port_info[UART_PORT_COUNT];
static uart_driver_port_stats_t port_stats[UART_PORT_COUNT];
static uint8_t port_status[UART_PORT_COUNT];
static usb_cdc_port_stats_t cdc_stats[UART_PORT_COUNT];
static uart_driver_control_history_t control_history[UART_PORT_COUNT];
static void *transfer_buffer;
static uint16_t transfer_length;

const uart_driver_port_info_t uart_board_ports[UART_PORT_COUNT] = {
    {.backend = UART_DRIVER_BACKEND_HW, .tx_pin = 0, .rx_pin = 1, .rts_pin = 3, .cts_pin = 2},
    {.backend = UART_DRIVER_BACKEND_HW, .tx_pin = 4, .rx_pin = 5, .rts_pin = 7, .cts_pin = 6},
    {.backend = UART_DRIVER_BACKEND_PIO, .tx_pin = 8, .rx_pin = 9, .rts_pin = UINT32_MAX, .cts_pin = UINT32_MAX},
    {.backend = UART_DRIVER_BACKEND_PIO, .tx_pin = 12, .rx_pin = 13, .rts_pin = UINT32_MAX, .cts_pin = UINT32_MAX},
    {.backend = UART_DRIVER_BACKEND_PIO, .tx_pin = 16, .rx_pin = 17, .rts_pin = UINT32_MAX, .cts_pin = UINT32_MAX},
    {.backend = UART_DRIVER_BACKEND_PIO, .tx_pin = 20, .rx_pin = 21, .rts_pin = UINT32_MAX, .cts_pin = UINT32_MAX},
};

bool boot_config_profile_supported(uint32_t profile) { return profile <= 4u; }
bool boot_config_set(const boot_config_t *config)
{
    staged_config = *config;
    return config_set_succeeds;
}
boot_config_save_result_t boot_config_save_with_result(void) { ++save_calls; return config_save_result; }
const boot_config_status_t *boot_config_get_status(void) { return &boot_status; }
void system_reset(void) { ++reset_calls; }
uint8_t system_reset_cause(void) { return 2u; }
bool tud_hid_ready(void) { return endpoint_ready; }
bool tud_hid_report(uint8_t id, void const *report, uint16_t length)
{
    if (!endpoint_accepts) return false;
    TEST_ASSERT_EQUAL_UINT16(30u, length);
    accepted_report_id = id;
    memcpy(accepted_payload, report, length);
    ++accepted_reports;
    return true;
}
bool led_read_state(bool *on) { *on = led_on; return led_available; }
void led_set(bool on) { TEST_ASSERT_TRUE(led_available); led_on = on; }
void pico_get_unique_board_id(pico_unique_board_id_t *id)
{
    const uint8_t bytes[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    memcpy(id->id, bytes, sizeof(bytes));
}
uint32_t clock_get_hz(unsigned int clock) { (void)clock; return clock_hz; }
float temperature_read_celsius(void) { return temperature; }
bool uart_driver_port_stats(uart_port_id_t port, uart_driver_port_stats_t *stats)
{
    *stats = port_stats[port];
    return stats_available; /* Deliberately mutate output even on failure to test defensive HID ownership. */
}
bool usb_cdc_port_stats(uint8_t port, usb_cdc_port_stats_t *stats)
{
    *stats = cdc_stats[port];
    stats->opened = cdc_open;
    return true;
}
bool uart_driver_control_history(uart_port_id_t port, uart_driver_control_history_t *history)
{
    *history = control_history[port];
    return true;
}
bool uart_driver_port_snapshot(uart_port_id_t port, uart_driver_port_info_t *info, uint8_t *status)
{
    *info = port_info[port];
    *status = port_status[port];
    return snapshot_available;
}
bool tud_control_xfer(uint8_t rhport, tusb_control_request_t const *request, void *buffer, uint16_t length)
{
    (void)rhport;
    (void)request;
    transfer_buffer = buffer;
    transfer_length = length;
    return true;
}
bool __real_hidd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    (void)rhport;
    (void)stage;
    (void)request;
    ++delegated_transfers;
    return true;
}

static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t get(uint8_t id, uint8_t *out)
{
    return tud_hid_get_report_cb(0u, id, HID_REPORT_TYPE_FEATURE, out, 64u);
}
static void command(uint8_t action, uint8_t flags, uint8_t profile)
{
    const uint8_t request[] = {1u, action, flags, profile};
    tud_hid_set_report_cb(0u, 20u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
}
static void result(uint8_t action, uint8_t status, uint8_t error)
{
    uint8_t out[64];
    const uint8_t expected[] = {1u, action, status, error};
    TEST_ASSERT_EQUAL_UINT16(4u, get(20u, out));
    TEST_ASSERT_EQUAL_MEMORY(expected, out, sizeof(expected));
}

void setUp(void)
{
    pico_test_time_us = 0u;
    service_load_init();
    led_available = stats_available = snapshot_available = endpoint_accepts = config_set_succeeds = true;
    led_on = cdc_open = endpoint_ready = false;
    temperature = 25.0f;
    clock_hz = 125000000u;
    accepted_reports = reset_calls = save_calls = delegated_transfers = 0u;
    transfer_buffer = NULL;
    transfer_length = 0u;
    boot_status = (boot_config_status_t){.stored_profile = BOOT_CONFIG_PROFILE_UNAVAILABLE};
    config_save_result = BOOT_CONFIG_SAVE_OK;
    memset(port_stats, 0, sizeof(port_stats));
    memset(cdc_stats, 0, sizeof(cdc_stats));
    memset(control_history, 0, sizeof(control_history));
    for (size_t i = 0; i < UART_PORT_COUNT; ++i) {
        port_info[i] = uart_board_ports[i];
        port_info[i].baud_rate = 115200u;
        port_info[i].data_bits = 8u;
        port_info[i].stop_bits = 1u;
        port_status[i] = UART_DRIVER_PORT_STATUS_READY;
    }
    usb_hid_init();
}
void tearDown(void) {}

static void test_identity_and_configuration_exact_layouts(void)
{
    uint8_t out[64];
    memset(out, 0xa5, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(27u, get(1u, out));
    const uint8_t prefix[] = {1, 1, 0, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    TEST_ASSERT_EQUAL_MEMORY(prefix, out, sizeof(prefix));
    TEST_ASSERT_EQUAL_UINT8(PICO_RP2350 ? 2u : 1u, out[14]);
    TEST_ASSERT_EQUAL_UINT8(PICO_UART_BOARD_MODEL, out[15]);
    TEST_ASSERT_EQUAL_HEX8(3u, out[16]);
    TEST_ASSERT_EQUAL_HEX8(PICO_UART_ALLOW_HID_RESET ? 7u : 5u, out[17]);
    TEST_ASSERT_EQUAL_HEX8(0x1fu, out[18]);
    TEST_ASSERT_EQUAL_UINT32(PICO_RP2350 ? 0x82000u : 0x42000u, u32(out + 19u));
    TEST_ASSERT_EQUAL_UINT32(0x2294u, u32(out + 23u));
    TEST_ASSERT_EQUAL_HEX8(0xa5u, out[27]);
    TEST_ASSERT_EQUAL_UINT16(36u, get(2u, out));
    TEST_ASSERT_EQUAL_UINT8(255u, out[1]);
    TEST_ASSERT_EQUAL_UINT8(6u, out[2]);
    const uint8_t pins[] = {0, 1, 3, 2, 4, 5, 7, 6, 8, 9, 255, 255,
                            12, 13, 255, 255, 16, 17, 255, 255, 20, 21, 255, 255};
    TEST_ASSERT_EQUAL_MEMORY(pins, out + 3u, sizeof(pins));
    TEST_ASSERT_EQUAL_UINT8(2u, out[27]);
    TEST_ASSERT_EQUAL_UINT16(4096u, u16(out + 28u));
    TEST_ASSERT_EQUAL_UINT16(4096u, u16(out + 30u));
    TEST_ASSERT_EQUAL_UINT16(2048u, u16(out + 32u));
    TEST_ASSERT_EQUAL_UINT16(2048u, u16(out + 34u));
}

static void test_telemetry_layout_failure_and_recovery(void)
{
    uint8_t out[64];
    TEST_ASSERT_EQUAL_UINT16(20u, get(3u, out));
    TEST_ASSERT_EQUAL_UINT16(2500u, u16(out + 2u));
    TEST_ASSERT_EQUAL_UINT16(65535u, u16(out + 12u));
    TEST_ASSERT_EQUAL_UINT32(125000000u, u32(out + 16u));
    temperature = NAN;
    memset(out, 0xa5, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(0u, get(3u, out));
    TEST_ASSERT_EQUAL_HEX8(0xa5u, out[0]);
    temperature = INFINITY;
    TEST_ASSERT_EQUAL_UINT16(0u, get(3u, out));
    temperature = -4.0f;
    TEST_ASSERT_EQUAL_UINT16(20u, get(3u, out));
    TEST_ASSERT_EQUAL_INT16(-400, (int16_t)u16(out + 2u));
    clock_hz = 0u;
    TEST_ASSERT_EQUAL_UINT16(0u, get(3u, out));
    clock_hz = 125000000u;
    led_available = false;
    TEST_ASSERT_EQUAL_UINT16(0u, get(3u, out));
    usb_hid_init();
    TEST_ASSERT_EQUAL_UINT16(20u, get(3u, out));
    TEST_ASSERT_EQUAL_UINT8(0u, out[1]);
}

static void test_channel_states_and_applied_settings(void)
{
    uint8_t out[64];
    const uint8_t status[] = {15u, 0u, 1u, 9u, 3u, 5u, 8u};
    const uint8_t expected[] = {4u, 1u, 2u, 3u, 4u, 4u, 1u};
    for (size_t i = 0u; i < sizeof(status); ++i) {
        port_status[0] = status[i];
        cdc_open = false;
        TEST_ASSERT_EQUAL_UINT16(30u, get(10u, out));
        TEST_ASSERT_EQUAL_UINT8(0u, out[1]);
        cdc_open = true;
        TEST_ASSERT_EQUAL_UINT16(30u, get(10u, out));
        TEST_ASSERT_EQUAL_UINT8(expected[i], out[1]);
        TEST_ASSERT_EQUAL_UINT8(1u, out[2]);
    }
    port_info[0].baud_rate = 230400u;
    port_info[0].data_bits = 7u;
    port_info[0].stop_bits = 2u;
    port_info[0].parity = UART_DRIVER_PARITY_EVEN;
    get(10u, out);
    TEST_ASSERT_EQUAL_UINT32(230400u, u32(out + 3u));
    TEST_ASSERT_EQUAL_UINT8(7u, out[7]);
    TEST_ASSERT_EQUAL_UINT8(2u, out[8]);
    TEST_ASSERT_EQUAL_UINT8(2u, out[9]);
    snapshot_available = false;
    TEST_ASSERT_EQUAL_UINT16(0u, get(10u, out));
}

static void test_accounting_control_reads_and_publication_commits(void)
{
    uint8_t out[64];
    port_stats[0].uart_tx_bytes = 25u;
    cdc_stats[0].rx_bytes = 50u;
    pico_test_time_us = 250000u;
    get(10u, out);
    TEST_ASSERT_EQUAL_UINT16(0u, u16(out + 10u));
    TEST_ASSERT_EQUAL_UINT32(25u, u32(out + 14u));
    get(30u, out);
    get(10u, out);
    TEST_ASSERT_EQUAL_UINT32(25u, u32(out + 14u));
    endpoint_ready = true;
    endpoint_accepts = false;
    pico_test_time_us = 500000u;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT16(0u, usb_hid_channels[0].sequence);
    usb_hid_next_channel = 0u;
    endpoint_accepts = true;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT8(10u, accepted_report_id);
    TEST_ASSERT_EQUAL_UINT16(1u, u16(accepted_payload + 10u));
    TEST_ASSERT_EQUAL_UINT16(500u, u16(accepted_payload + 12u));
    TEST_ASSERT_EQUAL_UINT32(25u, u32(accepted_payload + 14u));
    for (uint8_t i = 1u; i < 6u; ++i) {
        usb_hid_poll();
        TEST_ASSERT_EQUAL_UINT8(10u + i, accepted_report_id);
    }
    get(10u, out);
    TEST_ASSERT_EQUAL_UINT16(1u, u16(out + 10u));
    TEST_ASSERT_EQUAL_UINT16(0u, u16(out + 12u));
    TEST_ASSERT_EQUAL_UINT32(50u, u32(out + 26u));
}

static void test_interval_saturation_sequence_wrap_and_reconnect(void)
{
    uint8_t out[64];
    const uint32_t times[] = {0u, 1u, 65534u, 65535u, 70000u};
    const uint16_t intervals[] = {0u, 1u, 65534u, 65535u, 65535u};
    for (size_t i = 0u; i < 5u; ++i) {
        pico_test_time_us = (uint64_t)times[i] * 1000u;
        get(10u, out);
        TEST_ASSERT_EQUAL_UINT16(intervals[i], u16(out + 12u));
    }
    usb_hid_channels[0].sequence = UINT16_MAX;
    usb_hid_next_channel = 0u;
    endpoint_ready = true;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT16(0u, u16(accepted_payload + 10u));
    port_stats[0].uart_tx_bytes = UINT32_MAX - 1u;
    get(10u, out);
    port_stats[0].uart_tx_bytes = 32u;
    get(30u, out);
    get(10u, out);
    TEST_ASSERT_EQUAL_UINT32(32u, u32(out + 14u));
    TEST_ASSERT_TRUE(usb_hid_channels[0].total[0] > UINT32_MAX);
    usb_hid_reset_host_state();
    get(10u, out);
    TEST_ASSERT_EQUAL_UINT32(32u, u32(out + 14u));
    TEST_ASSERT_EQUAL_UINT16(0u, u16(out + 10u));
}

static void test_rejected_acquisition_cached_age_and_recovery(void)
{
    uint8_t out[64];
    stats_available = false;
    port_stats[0].uart_tx_bytes = 1000u;
    get(10u, out);
    TEST_ASSERT_EQUAL_UINT8(0u, out[2]);
    TEST_ASSERT_EQUAL_UINT32(0u, u32(out + 14u));
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT8(255u, out[2]);
    stats_available = true;
    port_stats[0].uart_tx_bytes = 10u;
    get(10u, out);
    stats_available = false;
    port_stats[0].uart_tx_bytes = 5u;
    port_stats[0].rx_error_count = 100u;
    port_stats[0].rx_ring_high_watermark = 4000u;
    cdc_stats[0].tx_bytes = 30u;
    pico_test_time_us = 99000u;
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT8(2u, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[2]);
    TEST_ASSERT_EQUAL_UINT32(0u, u32(out + 8u));
    TEST_ASSERT_EQUAL_UINT16(0u, u16(out + 18u));
    pico_test_time_us = 26000000u;
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT8(255u, out[2]);
    get(10u, out);
    TEST_ASSERT_EQUAL_UINT32(10u, u32(out + 14u));
    TEST_ASSERT_EQUAL_UINT32(30u, u32(out + 22u));
    stats_available = true;
    port_stats[0].uart_tx_bytes = 20u;
    get(10u, out);
    TEST_ASSERT_EQUAL_UINT8(1u, out[2]);
    TEST_ASSERT_EQUAL_UINT32(20u, u32(out + 14u));
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT8(0u, out[2]);
    TEST_ASSERT_EQUAL_UINT32(100u, u32(out + 8u));
}

static void test_diagnostic_history_peaks_and_independent_control_errors(void)
{
    uint8_t out[64];
    port_status[0] = UART_DRIVER_PORT_STATUS_INIT_FAILED;
    pico_test_time_us = 1000000u;
    usb_hid_poll();
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT32(0u, u32(out + 12u));
    port_stats[0].rx_ring_overflow_count = 3u;
    port_stats[0].rx_ring_pending_overflow_count = 2u;
    port_stats[0].rx_error_count = 7u;
    port_stats[0].tx_ring_high_watermark = 4096u;
    port_stats[0].rx_ring_high_watermark = 4095u;
    cdc_stats[0].tx_fifo_high_watermark = 2048u;
    cdc_stats[0].rx_fifo_high_watermark = 2047u;
    pico_test_time_us = 2000000u;
    usb_hid_poll(); /* Background collection requires neither an endpoint nor a diagnostic reader. */
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT32(5u, u32(out + 4u));
    TEST_ASSERT_EQUAL_UINT32(7u, u32(out + 8u));
    TEST_ASSERT_EQUAL_UINT32(2u, u32(out + 12u));
    TEST_ASSERT_EQUAL_UINT16(4096u, u16(out + 16u));
    TEST_ASSERT_EQUAL_UINT16(4095u, u16(out + 18u));
    TEST_ASSERT_EQUAL_UINT16(2048u, u16(out + 20u));
    TEST_ASSERT_EQUAL_UINT16(2047u, u16(out + 22u));
    control_history[0] = (uart_driver_control_history_t){UART_CONTROL_ERROR_TIMEOUT, 9000000u};
    stats_available = false;
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT8(4u, out[3]);
    TEST_ASSERT_EQUAL_UINT32(9u, u32(out + 12u));
    control_history[0].uptime_us = 3000000u;
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT32(9u, u32(out + 12u));
    usb_hid_reset_host_state();
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT32(5u, u32(out + 4u));
    stats_available = true;
    pico_test_time_us = ((uint64_t)UINT32_MAX + 1u) * 1000000u;
    port_stats[0].rx_error_count = UINT32_MAX;
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, u32(out + 8u));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, u32(out + 12u));
    port_stats[0].rx_error_count = 1u;
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, u32(out + 8u));
}

static void test_reason_only_history_and_first_second_zero(void)
{
    uint8_t out[64];
    stats_available = false;
    control_history[0] = (uart_driver_control_history_t){UART_CONTROL_ERROR_FRAMING, 900000u};
    pico_test_time_us = 900000u;
    get(30u, out);
    TEST_ASSERT_EQUAL_UINT8(0u, out[1]);
    TEST_ASSERT_EQUAL_UINT8(255u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(2u, out[3]);
    TEST_ASSERT_EQUAL_UINT32(0u, u32(out + 8u));
    TEST_ASSERT_EQUAL_UINT32(0u, u32(out + 12u));
}

static void test_action_validation_and_capabilities(void)
{
    result(0u, 0u, 0u);
    command(0x7fu, 0x80u, 0u); result(0u, 3u, 1u);
    command(0x7fu, 1u, 0u); result(0u, 4u, 5u);
    command(2u, 0x80u, 0u); result(2u, 3u, 1u);
    TEST_ASSERT_FALSE(led_on);
    command(2u, 0u, 0u); result(2u, 2u, 0u);
    TEST_ASSERT_TRUE(led_on);
    command(1u, 0u, 0u); result(1u, 2u, 0u);
    TEST_ASSERT_FALSE(led_on);
    command(4u, 0u, 255u); result(4u, 3u, 2u);
    command(4u, 0u, 1u); result(4u, 3u, 4u);
    command(4u, 1u, 1u); result(4u, 3u, 3u);
    command(4u, 3u, 0u); result(4u, 3u, 1u);
    cdc_open = true;
    command(4u, 1u, 0u); result(4u, 5u, 6u);
    TEST_ASSERT_EQUAL_UINT(0u, save_calls);
    led_available = false;
    usb_hid_init();
    command(2u, 0u, 0u); result(2u, 4u, 5u);
    command(3u, 0u, 0u);
    result(3u, PICO_UART_ALLOW_HID_RESET ? 3u : 4u, PICO_UART_ALLOW_HID_RESET ? 4u : 5u);
}

static void test_save_pending_execution_rechecks_and_errors(void)
{
    command(4u, 3u, 4u); result(4u, 1u, 0u);
    command(2u, 0u, 0u); result(4u, 1u, 0u);
    TEST_ASSERT_FALSE(led_on);
    cdc_open = true;
    usb_hid_poll(); result(4u, 5u, 6u);
    TEST_ASSERT_EQUAL_UINT(0u, save_calls);
    cdc_open = false;
    config_set_succeeds = false;
    command(4u, 1u, 0u);
    usb_hid_poll(); result(4u, 6u, 9u);
    config_set_succeeds = true;
    config_save_result = BOOT_CONFIG_SAVE_FLASH_FAILED;
    command(4u, 1u, 0u);
    usb_hid_poll(); result(4u, 6u, 7u);
    config_save_result = BOOT_CONFIG_SAVE_VERIFY_FAILED;
    command(4u, 1u, 0u);
    usb_hid_poll(); result(4u, 6u, 8u);
    config_save_result = BOOT_CONFIG_SAVE_OK;
    command(4u, 3u, 4u);
    usb_hid_poll(); result(4u, 2u, 0u);
    TEST_ASSERT_EQUAL_UINT8(4u, staged_config.clock_profile);
    absolute_time_t deadline = usb_hid_reboot_deadline;
    command(4u, 1u, 0u);
    usb_hid_reset_host_state();
    result(4u, 2u, 0u);
    TEST_ASSERT_EQUAL_MEMORY(&deadline, &usb_hid_reboot_deadline, sizeof(deadline));
    uint8_t out[64];
    get(2u, out);
    TEST_ASSERT_EQUAL_UINT8(255u, out[1]);
    pico_test_time_us = 999000u;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(0u, reset_calls);
    pico_test_time_us = 1000000u;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(1u, reset_calls);
}

static void test_reboot_confirmation_and_no_arm_transaction(void)
{
    command(3u, 1u, 0u);
#if PICO_UART_ALLOW_HID_RESET
    result(3u, 2u, 0u);
    TEST_ASSERT_TRUE(usb_hid_reboot_pending);
    pico_test_time_us = 1000000u;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(1u, reset_calls);
#else
    result(3u, 4u, 5u);
    TEST_ASSERT_FALSE(usb_hid_reboot_pending);
#endif
}

static tusb_control_request_t transfer(uint8_t id, hid_report_type_t type, bool input, uint16_t length)
{
    tusb_control_request_t request = {
        .bRequest = input ? HID_REQ_CONTROL_GET_REPORT : HID_REQ_CONTROL_SET_REPORT,
        .wValue = (uint16_t)((uint16_t)type << 8) | id,
        .wLength = length,
    };
    request.bmRequestType_bit.type = TUSB_REQ_TYPE_CLASS;
    request.bmRequestType_bit.direction = input ? TUSB_DIR_IN : TUSB_DIR_OUT;
    return request;
}

static void test_control_adapter_pre_ack_results_protected_writes_and_failed_gets(void)
{
    tusb_control_request_t request = transfer(1u, HID_REPORT_TYPE_FEATURE, true, 28u);
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    TEST_ASSERT_EQUAL_UINT16(28u, transfer_length);
    TEST_ASSERT_EQUAL_UINT8(1u, ((uint8_t *)transfer_buffer)[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, ((uint8_t *)transfer_buffer)[1]);
    TEST_ASSERT_EQUAL_UINT8(1u, ((uint8_t *)transfer_buffer)[2]);
    TEST_ASSERT_EQUAL_UINT8(0u, ((uint8_t *)transfer_buffer)[3]);
    TEST_ASSERT_EQUAL_UINT8(1u, ((uint8_t *)transfer_buffer)[4]);
    request.wLength = 27u;
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    TEST_ASSERT_EQUAL_UINT16(27u, transfer_length);
    TEST_ASSERT_EQUAL_UINT8(1u, ((uint8_t *)transfer_buffer)[0]);
    TEST_ASSERT_EQUAL_UINT8(0x23u, ((uint8_t *)transfer_buffer)[4]);
    request.wLength = 10u;
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    TEST_ASSERT_EQUAL_UINT16(10u, transfer_length);
    TEST_ASSERT_EQUAL_UINT8(1u, ((uint8_t *)transfer_buffer)[0]);

    request = transfer(20u, HID_REPORT_TYPE_FEATURE, false, 5u);
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    TEST_ASSERT_EQUAL_UINT16(5u, transfer_length);
    const uint8_t save[] = {20u, 1u, 4u, 1u, 0u};
    memcpy(transfer_buffer, save, sizeof(save));
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_DATA, &request));
    result(4u, 1u, 0u); /* Result exists before ACK. */
    TEST_ASSERT_FALSE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    result(4u, 1u, 0u);
    request = transfer(20u, HID_REPORT_TYPE_FEATURE, true, 5u);
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    TEST_ASSERT_EQUAL_UINT16(5u, transfer_length);
    TEST_ASSERT_EQUAL_UINT8(20u, ((uint8_t *)transfer_buffer)[0]);
    temperature = NAN;
    request = transfer(3u, HID_REPORT_TYPE_FEATURE, true, 21u);
    TEST_ASSERT_FALSE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    request.bRequest = 2u;
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    TEST_ASSERT_EQUAL_UINT(1u, delegated_transfers);
}

static void test_adapter_read_only_writes_malformed_and_report_types(void)
{
    uint8_t out[64];
    for (uint16_t id = 1u; id <= 255u; ++id) {
        bool assigned = id <= 3u || (id >= 10u && id <= 15u) || id == 20u || (id >= 30u && id <= 35u);
        TEST_ASSERT_EQUAL(assigned, get((uint8_t)id, out) != 0u);
        bool input = id >= 10u && id <= 15u;
        TEST_ASSERT_EQUAL(input,
            tud_hid_get_report_cb(0u, (uint8_t)id, HID_REPORT_TYPE_INPUT, out, 64u) != 0u);
        if (id != 20u) {
            tusb_control_request_t request = transfer((uint8_t)id, HID_REPORT_TYPE_FEATURE, false, 5u);
            TEST_ASSERT_FALSE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
        }
    }
    tusb_control_request_t request = transfer(20u, HID_REPORT_TYPE_FEATURE, false, 3u);
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    const uint8_t short_payload[] = {1u, 2u, 0u};
    memcpy(transfer_buffer, short_payload, sizeof(short_payload));
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_DATA, &request));
    result(0u, 3u, 1u);
    TEST_ASSERT_FALSE(led_on);
    request.wLength = 4u;
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    memcpy(transfer_buffer, short_payload, sizeof(short_payload)); /* Missing profile cannot become zero. */
    TEST_ASSERT_TRUE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_DATA, &request));
    result(2u, 3u, 1u);
    TEST_ASSERT_FALSE(led_on);
    request.wLength = 65u;
    TEST_ASSERT_FALSE(__wrap_hidd_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    result(0u, 3u, 1u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_identity_and_configuration_exact_layouts);
    RUN_TEST(test_telemetry_layout_failure_and_recovery);
    RUN_TEST(test_channel_states_and_applied_settings);
    RUN_TEST(test_accounting_control_reads_and_publication_commits);
    RUN_TEST(test_interval_saturation_sequence_wrap_and_reconnect);
    RUN_TEST(test_rejected_acquisition_cached_age_and_recovery);
    RUN_TEST(test_diagnostic_history_peaks_and_independent_control_errors);
    RUN_TEST(test_reason_only_history_and_first_second_zero);
    RUN_TEST(test_action_validation_and_capabilities);
    RUN_TEST(test_save_pending_execution_rechecks_and_errors);
    RUN_TEST(test_reboot_confirmation_and_no_arm_transaction);
    RUN_TEST(test_control_adapter_pre_ack_results_protected_writes_and_failed_gets);
    RUN_TEST(test_adapter_read_only_writes_malformed_and_report_types);
    return UNITY_END();
}
