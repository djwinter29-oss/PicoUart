/**
 * @file test_usb_hid.c
 * @brief Verify the production HID callbacks against the version-1 wire layouts.
 */

#include "unity.h"
#define __end__ pico_uart_test_static_end
#include "../src/usb/usb_hid.c"
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

absolute_time_t pico_test_time_us;
static bool led_available;
static bool led_on;
static bool cdc_open;
static bool endpoint_ready;
static bool endpoint_accepts;
static uint8_t accepted_report_id;
static uint8_t accepted_payload[USB_HID_CHANNEL_SIZE];
static unsigned int accepted_reports;
static unsigned int reset_calls;
static unsigned int save_calls;
static bool config_set_succeeds;
static boot_config_save_result_t config_save_result;
static boot_config_t staged_config;
static uart_driver_port_info_t port_info[UART_PORT_COUNT];
static uart_driver_port_stats_t port_stats[UART_PORT_COUNT];
static uint8_t port_status[UART_PORT_COUNT];
static usb_cdc_port_stats_t cdc_stats[UART_PORT_COUNT];
static boot_config_status_t boot_status;

bool boot_config_profile_supported(uint32_t profile)
{
    return profile <= BOOT_CLOCK_250_MHZ || (PICO_RP2350 && profile == BOOT_CLOCK_280_MHZ);
}

bool boot_config_set(const boot_config_t *config)
{
    if (!config_set_succeeds || config == NULL || !boot_config_profile_supported(config->clock_profile)) {
        return false;
    }
    staged_config = *config;
    return true;
}

boot_config_save_result_t boot_config_save_with_result(void)
{
    ++save_calls;
    return config_save_result;
}

bool boot_config_save(void)
{
    return boot_config_save_with_result() == BOOT_CONFIG_SAVE_OK;
}

void system_reset(void)
{
    ++reset_calls;
}

const boot_config_status_t *boot_config_get_status(void)
{
    return &boot_status;
}

bool tud_hid_ready(void)
{
    return endpoint_ready;
}

bool tud_hid_report(uint8_t report_id, void const *report, uint16_t len)
{
    if (!endpoint_accepts) {
        return false;
    }
    TEST_ASSERT_EQUAL_UINT16(USB_HID_CHANNEL_SIZE, len);
    accepted_report_id = report_id;
    memcpy(accepted_payload, report, len);
    ++accepted_reports;
    return true;
}

bool led_read_state(bool *on)
{
    if (on != NULL) {
        *on = led_on;
    }
    return led_available;
}

void led_set(bool on)
{
    TEST_ASSERT_TRUE(led_available);
    led_on = on;
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

bool uart_driver_port_stats(uart_port_id_t port, uart_driver_port_stats_t *stats)
{
    *stats = port_stats[port];
    return true;
}

bool usb_cdc_port_stats(uint8_t port, usb_cdc_port_stats_t *stats)
{
    *stats = cdc_stats[port];
    stats->opened = cdc_open;
    return true;
}

uint8_t uart_driver_port_status(uart_port_id_t port)
{
    return port_status[port];
}

bool uart_driver_port_snapshot(uart_port_id_t port, uart_driver_port_info_t *info, uint8_t *status)
{
    *info = port_info[port];
    *status = port_status[port];
    return true;
}

static void make_command(uint8_t payload[USB_HID_CONTROL_SIZE], uint8_t command, uint8_t flags, uint32_t id,
                         uint32_t profile)
{
    memset(payload, 0, USB_HID_CONTROL_SIZE);
    payload[0] = 1u;
    payload[1] = command;
    payload[2] = flags;
    usb_hid_write_u32(payload + 4u, id);
    usb_hid_write_u32(payload + 16u, profile);
}

static uint16_t get_report(uint8_t id, hid_report_type_t type, uint8_t *out)
{
    return tud_hid_get_report_cb(0u, id, type, out, 64u);
}

void setUp(void)
{
    pico_test_time_us = 0;
    service_load_init();
    led_available = true;
    led_on = false;
    cdc_open = false;
    endpoint_ready = false;
    endpoint_accepts = true;
    accepted_report_id = 0u;
    accepted_reports = 0u;
    reset_calls = 0u;
    save_calls = 0u;
    config_set_succeeds = true;
    config_save_result = BOOT_CONFIG_SAVE_OK;
    staged_config = (boot_config_t){.clock_profile = BOOT_CLOCK_DEFAULT};
    boot_status = (boot_config_status_t){
        .source = BOOT_CONFIG_DEFAULTS,
        .reason = BOOT_CONFIG_ERASED,
        .stored_profile = BOOT_CONFIG_PROFILE_UNAVAILABLE,
        .active_profile = BOOT_CLOCK_DEFAULT,
    };
    memset(port_stats, 0, sizeof(port_stats));
    memset(cdc_stats, 0, sizeof(cdc_stats));
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        port_info[index] = (uart_driver_port_info_t){
            .backend = index < 2u ? UART_DRIVER_BACKEND_HW : UART_DRIVER_BACKEND_PIO,
            .baud_rate = 115200u,
            .tx_pin = (uint32_t)index * 4u,
            .rx_pin = (uint32_t)index * 4u + 1u,
            .data_bits = 8u,
            .stop_bits = 1u,
            .parity = UART_DRIVER_PARITY_NONE,
        };
        port_status[index] = UART_DRIVER_PORT_STATUS_READY;
    }
    usb_hid_init();
}

void tearDown(void)
{
}

static void test_device_information_exact_63_byte_payload(void)
{
    uint8_t payload[64];
    memset(payload, 0xa5, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT16(63u, get_report(1u, HID_REPORT_TYPE_FEATURE, payload));
    TEST_ASSERT_EQUAL_UINT8(1u, payload[0]);
    TEST_ASSERT_EQUAL_MEMORY(PICO_UART_USB_PRODUCT, payload + 1u, sizeof(PICO_UART_USB_PRODUCT));
    TEST_ASSERT_EQUAL_HEX8_ARRAY("0123456789ABCDEF", payload + 33u, 16u);
    TEST_ASSERT_EQUAL_UINT8(6u, payload[53]);
    TEST_ASSERT_EQUAL_HEX8(0x03u, payload[54]);
    TEST_ASSERT_EQUAL_HEX8(0x3cu, payload[55]);
    TEST_ASSERT_EQUAL_HEX8(0x07u, payload[56]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, payload[57]);
#if PICO_RP2350
    TEST_ASSERT_EQUAL_UINT8(2u, payload[52]);
    TEST_ASSERT_EQUAL_HEX8(0x1fu, payload[58]);
    TEST_ASSERT_EQUAL_UINT32(0x82000u, usb_hid_read_u32(payload + 59u));
#else
    TEST_ASSERT_EQUAL_UINT8(1u, payload[52]);
    TEST_ASSERT_EQUAL_HEX8(0x0fu, payload[58]);
    TEST_ASSERT_EQUAL_UINT32(0x42000u, usb_hid_read_u32(payload + 59u));
#endif
    TEST_ASSERT_EQUAL_HEX8(0xa5u, payload[63]);
    TEST_ASSERT_EQUAL_UINT16(0u, get_report(1u, HID_REPORT_TYPE_INPUT, payload));
}

static void test_control_and_telemetry_layout_lengths(void)
{
    uint8_t payload[64];
    TEST_ASSERT_EQUAL_UINT16(24u, get_report(2u, HID_REPORT_TYPE_FEATURE, payload));
    const uint8_t control_prefix[] = {1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    TEST_ASSERT_EQUAL_MEMORY(control_prefix, payload, sizeof(control_prefix));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, usb_hid_read_u32(payload + 8u));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, usb_hid_read_u32(payload + 16u));
    TEST_ASSERT_EQUAL_HEX8(0u, payload[23]);

    TEST_ASSERT_EQUAL_UINT16(24u, get_report(3u, HID_REPORT_TYPE_FEATURE, payload));
    TEST_ASSERT_EQUAL_UINT8(1u, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, payload[1]);
    TEST_ASSERT_EQUAL_UINT16(2500u, (uint16_t)payload[2] | ((uint16_t)payload[3] << 8));
    TEST_ASSERT_EQUAL_UINT32(125000000u, usb_hid_read_u32(payload + 20u));
    TEST_ASSERT_EQUAL_UINT16(0u, get_report(3u, HID_REPORT_TYPE_INPUT, payload));
}

static void test_channel_feature_and_input_reads_are_non_consuming(void)
{
    uint8_t feature[64];
    uint8_t input[64];
    port_stats[0].controller_tx_bytes = 100u;
    port_stats[0].controller_rx_bytes = 200u;
    cdc_stats[0].tx_bytes = 300u;
    cdc_stats[0].rx_bytes = 400u;
    pico_test_time_us = 1000000;

    TEST_ASSERT_EQUAL_UINT16(54u, get_report(10u, HID_REPORT_TYPE_FEATURE, feature));
    TEST_ASSERT_EQUAL_UINT8(1u, feature[0]);
    TEST_ASSERT_EQUAL_UINT16(0u, (uint16_t)feature[1] | ((uint16_t)feature[2] << 8));
    TEST_ASSERT_EQUAL_UINT32(1000u, usb_hid_read_u32(feature + 3u));
    TEST_ASSERT_EQUAL_UINT32(100u, usb_hid_read_u32(feature + 9u));
    TEST_ASSERT_EQUAL_UINT32(200u, usb_hid_read_u32(feature + 13u));
    TEST_ASSERT_EQUAL_UINT32(300u, usb_hid_read_u32(feature + 17u));
    TEST_ASSERT_EQUAL_UINT32(400u, usb_hid_read_u32(feature + 21u));
    TEST_ASSERT_EQUAL_UINT32(115200u, usb_hid_read_u32(feature + 25u));
    TEST_ASSERT_EQUAL_UINT8(0u, feature[31]);
    TEST_ASSERT_EQUAL_UINT8(0u, feature[32]);
    TEST_ASSERT_EQUAL_UINT8(1u, feature[33]);
    TEST_ASSERT_EQUAL_UINT8(1u, feature[7]);

    TEST_ASSERT_EQUAL_UINT16(54u, get_report(10u, HID_REPORT_TYPE_INPUT, input));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(feature, input, sizeof(feature) < 54u ? sizeof(feature) : 54u);
    TEST_ASSERT_EQUAL_UINT16(54u, get_report(10u, HID_REPORT_TYPE_FEATURE, input));
    TEST_ASSERT_EQUAL_UINT32(100u, usb_hid_read_u32(input + 9u));
    TEST_ASSERT_EQUAL_UINT16(0u, get_report(16u, HID_REPORT_TYPE_FEATURE, input));
    for (uint8_t id = 11u; id <= 15u; ++id) {
        TEST_ASSERT_EQUAL_UINT16(54u, get_report(id, HID_REPORT_TYPE_FEATURE, input));
    }
}

static void test_channel_publication_commits_independent_sequence_and_baseline(void)
{
    uint8_t feature[64];
    port_stats[0].controller_tx_bytes = 25u;
    endpoint_ready = true;
    pico_test_time_us = 499000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(0u, accepted_reports);
    pico_test_time_us = 500000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(1u, accepted_reports);
    TEST_ASSERT_EQUAL_UINT8(10u, accepted_report_id);
    TEST_ASSERT_EQUAL_UINT16(1u, (uint16_t)accepted_payload[1] | ((uint16_t)accepted_payload[2] << 8));
    TEST_ASSERT_EQUAL_UINT32(500u, usb_hid_read_u32(accepted_payload + 3u));
    TEST_ASSERT_EQUAL_UINT32(25u, usb_hid_read_u32(accepted_payload + 9u));

    for (size_t channel = 1u; channel < UART_PORT_COUNT; ++channel) {
        usb_hid_traffic[channel].last_publication_ms = 1000u;
    }
    port_stats[0].controller_tx_bytes = 50u;
    pico_test_time_us = 600000;
    TEST_ASSERT_EQUAL_UINT16(54u, get_report(10u, HID_REPORT_TYPE_FEATURE, feature));
    TEST_ASSERT_EQUAL_UINT16(1u, (uint16_t)feature[1] | ((uint16_t)feature[2] << 8));
    TEST_ASSERT_EQUAL_UINT32(50u, usb_hid_read_u32(feature + 9u));
    endpoint_accepts = false;
    pico_test_time_us = 1000000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT16(1u, usb_hid_traffic[0].sequence);
    endpoint_accepts = true;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT16(2u, usb_hid_traffic[0].sequence);
    TEST_ASSERT_EQUAL_UINT16(10u, accepted_report_id);
}

static void test_boot_counter_wraps_on_wire(void)
{
    uint8_t payload[64];
    port_stats[0].controller_tx_bytes = UINT32_MAX - 15u;
    usb_hid_init();
    port_stats[0].controller_tx_bytes = 32u;
    pico_test_time_us = 1000;
    TEST_ASSERT_EQUAL_UINT16(54u, get_report(10u, HID_REPORT_TYPE_FEATURE, payload));
    TEST_ASSERT_EQUAL_UINT32(32u, usb_hid_read_u32(payload + 9u));
}

static void test_boot_counters_survive_usb_host_reset(void)
{
    uint8_t payload[64];
    port_stats[0].controller_tx_bytes = 10u;
    usb_hid_init();
    port_stats[0].controller_tx_bytes = 25u;
    usb_hid_reset_host_state();
    TEST_ASSERT_EQUAL_UINT16(54u, get_report(10u, HID_REPORT_TYPE_FEATURE, payload));
    TEST_ASSERT_EQUAL_UINT32(25u, usb_hid_read_u32(payload + 9u));
}

static void test_command_validation_precedence_echo_and_deduplication(void)
{
    uint8_t request[USB_HID_CONTROL_SIZE];
    uint8_t result[64];

    make_command(request, 5u, 0u, 11u, BOOT_CLOCK_150_MHZ);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_EQUAL_UINT16(24u, get_report(2u, HID_REPORT_TYPE_FEATURE, result));
    TEST_ASSERT_EQUAL_UINT8(3u, result[3]);
    TEST_ASSERT_EQUAL_UINT32(11u, usb_hid_read_u32(result + 4u));
    TEST_ASSERT_EQUAL_UINT32(BOOT_CLOCK_150_MHZ, usb_hid_read_u32(result + 16u));
    TEST_ASSERT_EQUAL_UINT8(3u, result[22]);

    make_command(request, 0x7fu, 1u, 12u, 0u);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_EQUAL_UINT16(24u, get_report(2u, HID_REPORT_TYPE_FEATURE, result));
    TEST_ASSERT_EQUAL_UINT8(4u, result[3]);
    TEST_ASSERT_EQUAL_UINT8(5u, result[22]);
    make_command(request, 0x7fu, 0x80u, 13u, 0u);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_EQUAL_UINT16(24u, get_report(2u, HID_REPORT_TYPE_FEATURE, result));
    TEST_ASSERT_EQUAL_UINT8(3u, result[3]);
    TEST_ASSERT_EQUAL_UINT8(1u, result[22]);
    TEST_ASSERT_EQUAL_UINT8(0x80u, result[2]);

    make_command(request, 2u, 0u, 14u, 0u);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_TRUE(led_on);
    make_command(request, 1u, 0u, 14u, 0u);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_TRUE(led_on);
}

static void test_save_pending_verified_completion_and_error_results(void)
{
    uint8_t request[USB_HID_CONTROL_SIZE];
    uint8_t result[64];
    make_command(request, 5u, 1u, 21u, BOOT_CLOCK_150_MHZ);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_EQUAL_UINT16(24u, get_report(2u, HID_REPORT_TYPE_FEATURE, result));
    TEST_ASSERT_EQUAL_UINT8(1u, result[3]);
    TEST_ASSERT_EQUAL_UINT8(0u, save_calls);
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT8(2u, usb_hid_command_status);
    TEST_ASSERT_EQUAL_UINT(1u, save_calls);
    TEST_ASSERT_EQUAL_UINT32(BOOT_CLOCK_150_MHZ, staged_config.clock_profile);
    TEST_ASSERT_TRUE(usb_hid_reboot_pending);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_EQUAL_UINT(1u, save_calls);
    pico_test_time_us = 999000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(0u, reset_calls);
    pico_test_time_us = 1000000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(1u, reset_calls);

    usb_hid_init();
    config_save_result = BOOT_CONFIG_SAVE_VERIFY_FAILED;
    make_command(request, 5u, 0u, 22u, BOOT_CLOCK_DEFAULT);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT8(6u, usb_hid_command_status);
    TEST_ASSERT_EQUAL_UINT8(8u, usb_hid_error_reason);
    TEST_ASSERT_FALSE(usb_hid_reboot_pending);

    usb_hid_init();
    config_save_result = BOOT_CONFIG_SAVE_FLASH_FAILED;
    make_command(request, 5u, 0u, 23u, BOOT_CLOCK_DEFAULT);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT8(6u, usb_hid_command_status);
    TEST_ASSERT_EQUAL_UINT8(7u, usb_hid_error_reason);
}

static void test_save_busy_and_malformed_requests_have_no_side_effects(void)
{
    uint8_t request[USB_HID_CONTROL_SIZE];
    uint8_t result[64];
    cdc_open = true;
    make_command(request, 5u, 0u, 31u, BOOT_CLOCK_DEFAULT);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_EQUAL_UINT16(24u, get_report(2u, HID_REPORT_TYPE_FEATURE, result));
    TEST_ASSERT_EQUAL_UINT8(5u, result[3]);
    TEST_ASSERT_EQUAL_UINT8(6u, result[22]);
    TEST_ASSERT_EQUAL_UINT(0u, save_calls);

    cdc_open = false;
    request[3] = 1u;
    usb_hid_write_u32(request + 4u, 32u);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_EQUAL_UINT8(3u, usb_hid_command_status);
    TEST_ASSERT_EQUAL_UINT8(1u, usb_hid_error_reason);
    TEST_ASSERT_EQUAL_UINT(0u, save_calls);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request) - 1u);
    TEST_ASSERT_EQUAL_UINT8(0u, usb_hid_command);
    TEST_ASSERT_EQUAL_UINT32(0u, usb_hid_request_id);
}

static void test_standalone_reboot_requires_fresh_arm_and_is_deduplicated(void)
{
    uint8_t request[USB_HID_CONTROL_SIZE];
    make_command(request, 3u, 0u, 41u, 0u);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
#if PICO_UART_ALLOW_HID_RESET
    const absolute_time_t arm_deadline = usb_hid_arm_deadline;
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_EQUAL_MEMORY(&arm_deadline, &usb_hid_arm_deadline, sizeof(arm_deadline));
    make_command(request, 4u, 0u, 42u, 0u);
    tud_hid_set_report_cb(0u, 2u, HID_REPORT_TYPE_FEATURE, request, sizeof(request));
    TEST_ASSERT_TRUE(usb_hid_reboot_pending);
    pico_test_time_us = 1000000;
    usb_hid_poll();
    TEST_ASSERT_EQUAL_UINT(1u, reset_calls);
#else
    TEST_ASSERT_EQUAL_UINT8(USB_HID_STATUS_UNSUPPORTED, usb_hid_command_status);
#endif
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_device_information_exact_63_byte_payload);
    RUN_TEST(test_control_and_telemetry_layout_lengths);
    RUN_TEST(test_channel_feature_and_input_reads_are_non_consuming);
    RUN_TEST(test_channel_publication_commits_independent_sequence_and_baseline);
    RUN_TEST(test_boot_counter_wraps_on_wire);
    RUN_TEST(test_boot_counters_survive_usb_host_reset);
    RUN_TEST(test_command_validation_precedence_echo_and_deduplication);
    RUN_TEST(test_save_pending_verified_completion_and_error_results);
    RUN_TEST(test_save_busy_and_malformed_requests_have_no_side_effects);
    RUN_TEST(test_standalone_reboot_requires_fresh_arm_and_is_deduplicated);
    return UNITY_END();
}
