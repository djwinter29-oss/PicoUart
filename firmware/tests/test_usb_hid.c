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
static bool led_manual_on;
static bool led_output_on;
static bool led_available;

bool led_read_state(bool *manual_on, bool *output_on)
{
    *manual_on = led_manual_on;
    *output_on = led_output_on;
    return led_available;
}

void setUp(void)
{
    pico_test_time_us = 0;
    service_load_init();
    led_manual_on = false;
    led_output_on = false;
    led_available = true;
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
    memset(stats, 0, sizeof(*stats));
    return true;
}

bool usb_cdc_port_stats(uint8_t itf, usb_cdc_port_stats_t *stats)
{
    (void)itf;
    memset(stats, 0, sizeof(*stats));
    return true;
}

bool uart_driver_port_info(uart_port_id_t port_id, uart_driver_port_info_t *info)
{
    (void)port_id;
    memset(info, 0, sizeof(*info));
    return true;
}

uint8_t uart_driver_port_status(uart_port_id_t port_id)
{
    (void)port_id;
    return 0u;
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
        2, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0, 0,
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

static void test_legacy_report_lengths(void)
{
    uint8_t buffer[64];
    TEST_ASSERT_EQUAL_UINT16(63u, tud_hid_get_report_cb(0, 1, HID_REPORT_TYPE_INPUT, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT16(8u, tud_hid_get_report_cb(0, 3, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT16(25u, tud_hid_get_report_cb(0, 5, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT16(6u, tud_hid_get_report_cb(0, 6, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT16(0u, tud_hid_get_report_cb(0, 10, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
}

static void test_led_payload(void)
{
    uint8_t buffer[3] = {0, 0, 0xa5};
    for (unsigned int flags = 0; flags < 4u; ++flags) {
        led_manual_on = (flags & 1u) != 0u;
        led_output_on = (flags & 2u) != 0u;
        TEST_ASSERT_EQUAL_UINT16(2u, tud_hid_get_report_cb(0, 9, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
        TEST_ASSERT_EQUAL_UINT8(1u, buffer[0]);
        TEST_ASSERT_EQUAL_UINT8(1u | (flags << 1u), buffer[1]);
        TEST_ASSERT_EQUAL_UINT8(0xa5, buffer[2]);
    }
    led_available = false;
    TEST_ASSERT_EQUAL_UINT16(2u, tud_hid_get_report_cb(0, 9, HID_REPORT_TYPE_FEATURE, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT8(0u, buffer[1]);
    TEST_ASSERT_EQUAL_UINT16(1u, tud_hid_get_report_cb(0, 9, HID_REPORT_TYPE_FEATURE, buffer, 1u));
    TEST_ASSERT_EQUAL_UINT16(0u, tud_hid_get_report_cb(0, 9, HID_REPORT_TYPE_INPUT, buffer, sizeof(buffer)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_identity_payload);
    RUN_TEST(test_runtime_payload);
    RUN_TEST(test_legacy_report_lengths);
    RUN_TEST(test_led_payload);
    return UNITY_END();
}
