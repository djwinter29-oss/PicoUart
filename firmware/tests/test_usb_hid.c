/**
 * @file test_usb_hid.c
 * @brief Exercise the production HID callback, payload offsets and SDK/linker value sources on the host.
 */

#include "unity.h"
/* Native linker scripts unconditionally define __end__; use a fixture-only name for that SDK symbol. */
#define __end__ pico_uart_test_static_end
#include "../src/usb/usb_hid.c"
#undef __end__

/* Fixed link-time extents, including nonzero scratch data and both stacks. */
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

absolute_time_t pico_test_time_us;
static bool led_on;
static bool led_available;
static uart_driver_port_stats_t diagnostic_stats;
static uint8_t diagnostic_health;
static boot_config_status_t configuration;
static bool cdc_open;
static bool save_success;
static bool set_success;
static boot_config_t staged_configuration;
static unsigned int save_calls;
static unsigned int reset_calls;
static uint32_t saved_profile;

bool boot_config_profile_supported(uint32_t profile)
{
    return profile <= BOOT_CLOCK_250_MHZ || (PICO_RP2350 && profile == BOOT_CLOCK_280_MHZ);
}

bool boot_config_set(const boot_config_t *config)
{
    if (!set_success || config == NULL || !boot_config_profile_supported((uint32_t)config->clock_profile)) {
        return false;
    }
    staged_configuration = *config;
    return true;
}

bool boot_config_save(void)
{
    ++save_calls;
    saved_profile = (uint32_t)staged_configuration.clock_profile;
    return save_success;
}

void system_reset(void)
{
    ++reset_calls;
}

void led_set(bool on)
{
    TEST_ASSERT_TRUE(led_available);
    led_on = on;
}

const boot_config_status_t *boot_config_get_status(void)
{
    return &configuration;
}

bool tud_hid_ready(void)
{
    return false;
}

bool tud_hid_report(uint8_t report_id, void const *report, uint16_t len)
{
    (void)report_id;
    (void)report;
    (void)len;
    TEST_FAIL_MESSAGE("The unavailable interrupt endpoint must not publish reports");
    return false;
}

bool led_read_state(bool *on)
{
    *on = led_on;
    return led_available;
}

void setUp(void)
{
    pico_test_time_us = 0;
    service_load_init();
    led_on = false;
    led_available = true;
    memset(&diagnostic_stats, 0, sizeof(diagnostic_stats));
    diagnostic_health = UART_DRIVER_PORT_STATUS_READY;
    cdc_open = false;
    save_success = true;
    set_success = true;
    staged_configuration = (boot_config_t){.clock_profile = BOOT_CLOCK_DEFAULT};
    save_calls = 0u;
    reset_calls = 0u;
    configuration = (boot_config_status_t){
        .source = BOOT_CONFIG_DEFAULTS, .reason = BOOT_CONFIG_ERASED,
        .stored_profile = BOOT_CONFIG_PROFILE_UNAVAILABLE, .active_profile = BOOT_CLOCK_DEFAULT,
    };
    usb_hid_init();
}

void tearDown(void)
{
}

void pico_get_unique_board_id_string(char *id_out, size_t len)
{
    TEST_ASSERT_EQUAL_UINT(17u, len);
    memcpy(id_out, "0123456789ABCDEF", len);
}

uint32_t clock_get_hz(unsigned int clock)
{
    (void)clock;
    return 125000000u;
}

float temperature_read_celsius(void)
{
    return 25.0f;
}

bool uart_driver_port_stats(uart_port_id_t port_id, uart_driver_port_stats_t *stats)
{
    (void)port_id;
    *stats = diagnostic_stats;
    return true;
}

bool usb_cdc_port_stats(uint8_t itf, usb_cdc_port_stats_t *stats)
{
    (void)itf;
    memset(stats, 0, sizeof(*stats));
    stats->opened = cdc_open;
    return true;
}

bool uart_driver_port_info(uart_port_id_t port_id, uart_driver_port_info_t *info)
{
    (void)port_id;
    memset(info, 0, sizeof(*info));
    info->baud_rate = 115200u;
    info->data_bits = 8u;
    info->stop_bits = 1u;
    info->tx_pin = (uint32_t)port_id * 4u;
    info->rx_pin = info->tx_pin + 1u;
    return true;
}

uint8_t uart_driver_port_status(uart_port_id_t port_id)
{
    (void)port_id;
    return diagnostic_health;
}

static void test_identity_payload(void)
{
    uint8_t buffer[64];
    uint8_t expected[49] = {1u};
    memcpy(expected + 1u, PICO_UART_USB_PRODUCT, sizeof(PICO_UART_USB_PRODUCT));
    memcpy(expected + 33u, "0123456789ABCDEF", 16u);

    memset(buffer, 0xa5, sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT16(49u, tud_hid_get_report_cb(0, 7, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buffer, sizeof(expected));
    TEST_ASSERT_EQUAL_HEX8(0xa5, buffer[49]);
    TEST_ASSERT_EQUAL_UINT16(5u, tud_hid_get_report_cb(0, 7, HID_REPORT_TYPE_FEATURE, buffer, 5u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buffer, 5u);
    TEST_ASSERT_EQUAL_UINT16(0u, tud_hid_get_report_cb(0, 7, HID_REPORT_TYPE_INPUT, buffer, sizeof(buffer)));
}

static void test_runtime_payload(void)
{
    uint8_t buffer[64];
    /* More than 2^32 ms; verify all eight uptime bytes and little-endian encoding. */
    pico_test_time_us = INT64_C(0x010203040506) * 1000 + 999;
    const uint8_t expected[21] = {
        1, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0, 0,
        0xc4, 0x09, 0xff, 0xff, /* Core 0 measured 25%; core 1 has no completed window. */
        0x94, 0x22, 0, 0,       /* Static 0x1234 + scratch 0x60 + stacks 0x1000. */
#if PICO_RP2350
        0, 0x20, 0x08, 0,
#else
        0, 0x20, 0x04, 0,
#endif
    };
    service_load_begin(0u, 0u);
    service_load_note(0u);
    service_load_end(0u, 250000u);
    service_load_begin(0u, 250000u);
    service_load_end(0u, 1000000u);
    memset(buffer, 0xa5, sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT16(21u, tud_hid_get_report_cb(0, 8, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buffer, sizeof(expected));
    TEST_ASSERT_EQUAL_HEX8(0xa5, buffer[21]);
    TEST_ASSERT_EQUAL_UINT16(3u, tud_hid_get_report_cb(0, 8, HID_REPORT_TYPE_FEATURE, buffer, 3u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buffer, 3u);
    TEST_ASSERT_EQUAL_UINT16(0u, tud_hid_get_report_cb(0, 8, HID_REPORT_TYPE_FEATURE, buffer, 0u));
    TEST_ASSERT_EQUAL_UINT16(0u, tud_hid_get_report_cb(0, 8, HID_REPORT_TYPE_INPUT, buffer, sizeof(buffer)));

    pico_test_time_us = 0;
    TEST_ASSERT_EQUAL_UINT16(21u, tud_hid_get_report_cb(0, 8, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    const uint8_t zero_uptime[8] = {0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero_uptime, buffer + 1u, sizeof(zero_uptime));
}

static void test_report_lengths(void)
{
    uint8_t buffer[64];
    TEST_ASSERT_EQUAL_UINT16(63u, tud_hid_get_report_cb(0, 1, HID_REPORT_TYPE_INPUT, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT8(1u, buffer[1]);
    TEST_ASSERT_EQUAL_UINT16(8u, tud_hid_get_report_cb(0, 3, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT8(1u, buffer[0]);
    TEST_ASSERT_EQUAL_UINT16(25u, tud_hid_get_report_cb(0, 5, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT8(1u, buffer[0]);
    TEST_ASSERT_EQUAL_UINT16(6u, tud_hid_get_report_cb(0, 6, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT16(12u, tud_hid_get_report_cb(0, 16, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    const uint8_t expected_config[] = {1, 0, 1, 0, 255, 255, 255, 255, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_MEMORY(expected_config, buffer, sizeof(expected_config));
    TEST_ASSERT_EQUAL_UINT16(3u, tud_hid_get_report_cb(0, 16, HID_REPORT_TYPE_FEATURE, buffer, 3u));
    TEST_ASSERT_EQUAL_UINT16(0u, tud_hid_get_report_cb(0, 16, HID_REPORT_TYPE_INPUT, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT16(8u, tud_hid_get_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    const uint8_t expected_save[] = {1, 0, 0, 0, 255, 255, 255, 255};
    TEST_ASSERT_EQUAL_MEMORY(expected_save, buffer, sizeof(expected_save));
}

static void test_configuration_save_validation_and_deferred_result(void)
{
    uint8_t request[9] = {1, 1, 0, 0, BOOT_CLOCK_250_MHZ, 0, 0, 0, 0};
    uint8_t result[8];
    for (uint16_t size = 0u; size <= 9u; ++size) {
        if (size == 8u) continue;
        tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, request, size);
        usb_hid_poll();
        TEST_ASSERT_EQUAL_UINT(0u, save_calls);
        TEST_ASSERT_EQUAL(USB_HID_CONFIG_INVALID, usb_hid_config_save_state);
    }
    for (unsigned int field = 0u; field < 8u; ++field) {
        uint8_t malformed[8];
        memcpy(malformed, request, sizeof(malformed));
        malformed[field] ^= 0x80;
        tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, malformed, sizeof(malformed));
        usb_hid_poll();
        TEST_ASSERT_EQUAL_UINT(0u, save_calls);
        TEST_ASSERT_EQUAL(USB_HID_CONFIG_INVALID, usb_hid_config_save_state);
    }
    request[1] = 0;
    tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, request, 8);
    TEST_ASSERT_EQUAL(USB_HID_CONFIG_INVALID, usb_hid_config_save_state);
    request[1] = 1;
    request[4] = PICO_RP2350 ? 5u : BOOT_CLOCK_280_MHZ;
    tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, request, 8);
    TEST_ASSERT_EQUAL(USB_HID_CONFIG_INVALID, usb_hid_config_save_state);
    request[4] = BOOT_CLOCK_250_MHZ;
    tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, request, 8);
    TEST_ASSERT_EQUAL_UINT(0u, save_calls);
    TEST_ASSERT_EQUAL(USB_HID_CONFIG_PENDING, usb_hid_config_save_state);
    uint8_t replacement[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, replacement, sizeof(replacement));
    TEST_ASSERT_EQUAL_UINT32(request[4], usb_hid_config_save_profile);
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(1u, save_calls);
    TEST_ASSERT_EQUAL_UINT(request[4], saved_profile);
    TEST_ASSERT_EQUAL(USB_HID_CONFIG_SAVED, usb_hid_config_save_state);
    TEST_ASSERT_EQUAL_UINT16(8, tud_hid_get_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, result, sizeof(result)));
    TEST_ASSERT_EQUAL_UINT8(2, result[1]);
    TEST_ASSERT_EQUAL_UINT8(request[4], result[4]);
    TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, configuration.active_profile);
    TEST_ASSERT_EQUAL_UINT(0u, reset_calls);
    pico_test_time_us = 999999;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(0u, reset_calls);
    pico_test_time_us = 1000000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(1u, reset_calls);
}

static void test_configuration_save_busy_failure_and_default(void)
{
    uint8_t request[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    cdc_open = true;
    tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    usb_hid_poll();
    TEST_ASSERT_EQUAL(USB_HID_CONFIG_BUSY, usb_hid_config_save_state);
    TEST_ASSERT_EQUAL_UINT(0u, save_calls);
    cdc_open = false;
    tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    cdc_open = true;
    usb_hid_poll();
    TEST_ASSERT_EQUAL(USB_HID_CONFIG_BUSY, usb_hid_config_save_state);
    TEST_ASSERT_EQUAL_UINT(0u, save_calls);
    cdc_open = false;
    set_success = false;
    tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    usb_hid_poll();
    TEST_ASSERT_EQUAL(USB_HID_CONFIG_FAILED, usb_hid_config_save_state);
    TEST_ASSERT_EQUAL_UINT(0u, save_calls);
    set_success = true;
    save_success = false;
    tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    usb_hid_poll();
    TEST_ASSERT_EQUAL(USB_HID_CONFIG_FAILED, usb_hid_config_save_state);
    save_success = true;
    tud_hid_set_report_cb(0, 17, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    usb_hid_poll();
    TEST_ASSERT_EQUAL(USB_HID_CONFIG_SAVED, usb_hid_config_save_state);
    TEST_ASSERT_EQUAL_UINT32(BOOT_CLOCK_DEFAULT, saved_profile);
}

static void test_channel_diagnostics_history(void)
{
    uint8_t buffer[64];
    usb_hid_channel_diagnostics_t report;
    diagnostic_stats.rx_error_count = 3u;
    diagnostic_stats.rx_ring_pending_overflow_count = 7u;
    diagnostic_health |= UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    pico_test_time_us = INT64_C(0x100000001) * 1000;
    memset(buffer, 0xa5, sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT16(59u, tud_hid_get_report_cb(0, 10, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    memcpy(&report, buffer, sizeof(report));
    TEST_ASSERT_EQUAL_UINT8(1u, report.version);
    TEST_ASSERT_EQUAL_UINT32(115200u, report.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(8u, report.data_bits);
    TEST_ASSERT_EQUAL_UINT8(1u, report.stop_bits);
    TEST_ASSERT_EQUAL_UINT8(0u, report.tx_pin);
    TEST_ASSERT_EQUAL_UINT8(1u, report.rx_pin);
    TEST_ASSERT_EQUAL_UINT32(1u, report.faults[1].count);
    TEST_ASSERT_EQUAL_UINT32(7u, report.faults[2].count);
    TEST_ASSERT_EQUAL_UINT32(3u, report.faults[3].count);
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x100000001), report.faults[3].last_seen_ms);
    TEST_ASSERT_EQUAL_HEX8(0xa5, buffer[59]);
    pico_test_time_us += 1000000;
    diagnostic_stats.rx_ring_overflow_count = 7u;
    diagnostic_stats.rx_ring_pending_overflow_count = 0u;
    diagnostic_health &= (uint8_t)~UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    (void)tud_hid_get_report_cb(0, 10, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer));
    memcpy(&report, buffer, sizeof(report));
    TEST_ASSERT_EQUAL_UINT32(7u, report.faults[2].count);
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x100000001), report.faults[3].last_seen_ms);
    diagnostic_stats.rx_error_count++;
    diagnostic_health |= UART_DRIVER_PORT_STATUS_CONTROL_ERROR;
    (void)tud_hid_get_report_cb(0, 10, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer));
    memcpy(&report, buffer, sizeof(report));
    TEST_ASSERT_EQUAL_UINT32(2u, report.faults[1].count);
    TEST_ASSERT_EQUAL_UINT32(4u, report.faults[3].count);
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x1000003e9), report.faults[3].last_seen_ms);
    TEST_ASSERT_EQUAL_UINT16(2u, tud_hid_get_report_cb(0, 15, HID_REPORT_TYPE_FEATURE, buffer, 2u));
    TEST_ASSERT_EQUAL_UINT16(0u, tud_hid_get_report_cb(0, 10, HID_REPORT_TYPE_INPUT, buffer, sizeof(buffer)));
}

static void test_channel_diagnostics_sample_without_interrupt_endpoint(void)
{
    diagnostic_stats.rx_error_count = 2u;
    diagnostic_health |= UART_DRIVER_PORT_STATUS_INIT_FAILED;
    pico_test_time_us = 100000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT32(2u, usb_hid_diagnostics[0].faults[3].count);
    TEST_ASSERT_EQUAL_UINT64(100u, usb_hid_diagnostics[0].faults[3].last_seen_ms);
    diagnostic_stats.rx_error_count = 5u;
    pico_test_time_us = 150000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT32(2u, usb_hid_diagnostics[0].faults[3].count);
    pico_test_time_us = 200000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT32(5u, usb_hid_diagnostics[0].faults[3].count);
    TEST_ASSERT_EQUAL_UINT32(1u, usb_hid_diagnostics[0].faults[0].count);
    TEST_ASSERT_EQUAL_UINT64(200u, usb_hid_diagnostics[0].faults[3].last_seen_ms);
    usb_hid_diagnostics[0].faults[3].count = UINT32_MAX - 1u;
    diagnostic_stats.rx_error_count = 10u;
    pico_test_time_us = 300000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, usb_hid_diagnostics[0].faults[3].count);
}

static void test_led_payload(void)
{
    uint8_t buffer[3] = {0, 0, 0xa5};
    for (unsigned int state = 0; state < 2u; ++state) {
        led_on = state != 0u;
        TEST_ASSERT_EQUAL_UINT16(2u, tud_hid_get_report_cb(0, 9, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
        TEST_ASSERT_EQUAL_UINT8(1u, buffer[0]);
        TEST_ASSERT_EQUAL_UINT8(state ? 7u : 1u, buffer[1]);
        TEST_ASSERT_EQUAL_UINT8(0xa5, buffer[2]);
    }
    led_available = false;
    TEST_ASSERT_EQUAL_UINT16(2u, tud_hid_get_report_cb(0, 9, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT8(0u, buffer[1]);
    TEST_ASSERT_EQUAL_UINT16(1u, tud_hid_get_report_cb(0, 9, HID_REPORT_TYPE_FEATURE, buffer, 1u));
    TEST_ASSERT_EQUAL_UINT16(0u, tud_hid_get_report_cb(0, 9, HID_REPORT_TYPE_INPUT, buffer, sizeof(buffer)));
}

static void test_identification_toggle_and_usb_reset(void)
{
    const uint8_t command[] = {USB_HID_COMMAND_TOGGLE_LED};
    tud_hid_set_report_cb(0, USB_HID_REPORT_ID_COMMAND, HID_REPORT_TYPE_FEATURE, command, sizeof(command));
    TEST_ASSERT_TRUE(led_on);
    usb_hid_reset_host_state();
    TEST_ASSERT_TRUE(led_on);
    tud_hid_set_report_cb(0, USB_HID_REPORT_ID_COMMAND, HID_REPORT_TYPE_FEATURE, command, sizeof(command));
    TEST_ASSERT_FALSE(led_on);
    led_available = false;
    tud_hid_set_report_cb(0, USB_HID_REPORT_ID_COMMAND, HID_REPORT_TYPE_FEATURE, command, sizeof(command));
    TEST_ASSERT_FALSE(led_on);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_identity_payload);
    RUN_TEST(test_runtime_payload);
    RUN_TEST(test_report_lengths);
    RUN_TEST(test_configuration_save_validation_and_deferred_result);
    RUN_TEST(test_configuration_save_busy_failure_and_default);
    RUN_TEST(test_led_payload);
    RUN_TEST(test_identification_toggle_and_usb_reset);
    RUN_TEST(test_channel_diagnostics_history);
    RUN_TEST(test_channel_diagnostics_sample_without_interrupt_endpoint);
    return UNITY_END();
}
