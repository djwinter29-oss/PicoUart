/**
 * @file usb_hid.c
 * @brief TinyUSB HID monitor helpers for PicoUart status reporting.
 */

#include "usb/usb_hid.h"

#include "config/config.h"
#include "driver/led.h"
#include "driver/system.h"
#include "driver/temperature.h"
#include "uart/uart_driver.h"
#include "usb/usb_cdc.h"

#include "pico/stdlib.h"
#include "tusb.h"

#include <limits.h>
#include <string.h>

#ifndef PICO_UART_VERSION_MAJOR
#define PICO_UART_VERSION_MAJOR 0u
#endif
#ifndef PICO_UART_VERSION_MINOR
#define PICO_UART_VERSION_MINOR 0u
#endif
#ifndef PICO_UART_VERSION_PATCH
#define PICO_UART_VERSION_PATCH 0u
#endif

/** @brief HID status report interval in milliseconds. */
#define USB_HID_STATUS_INTERVAL_MS 100u
/** @brief HID status report signature byte (`P`). */
#define USB_HID_SIGNATURE0 'P'
/**
 * @brief HID status / board-status report format version.
 *
 * v15 shrinks the status input payload to 63 bytes so Report ID + payload fit
 * in one full-speed HID interrupt packet (`CFG_TUD_HID_EP_BUFSIZE`).
 */
#define USB_HID_REPORT_VERSION 15u
/** @brief HID input report ID for the compact status monitor. */
#define USB_HID_REPORT_ID_STATUS 1u
/** @brief HID feature report ID for board temperature and firmware version. */
#define USB_HID_REPORT_ID_BOARD_STATUS 3u
/** @brief HID feature report ID for board control commands. */
#define USB_HID_REPORT_ID_COMMAND 4u

/** @brief HID command value that toggles the board LED. */
#define USB_HID_COMMAND_TOGGLE_LED 1u
/**
 * @brief HID command value that resets the board after a prior arm command.
 *
 * Remote reset is **disabled by default** (`PICO_UART_ALLOW_HID_RESET` is 0).
 * Compile with `-DPICO_UART_ALLOW_HID_RESET=1` to enable it on trusted hosts.
 * When enabled, @ref USB_HID_COMMAND_ARM_RESET must be sent first and
 * @ref USB_HID_COMMAND_RESET_BOARD must follow within
 * @ref USB_HID_RESET_ARM_WINDOW_MS.
 */
#define USB_HID_COMMAND_RESET_BOARD 2u
/** @brief HID command value that arms a subsequent reset command. */
#define USB_HID_COMMAND_ARM_RESET 3u
/** @brief Maximum time between arm-reset and reset commands. */
#define USB_HID_RESET_ARM_WINDOW_MS 2000u

#ifndef PICO_UART_ALLOW_HID_RESET
#define PICO_UART_ALLOW_HID_RESET 0
#endif

/** @brief Per-channel health bit: the host opened the matching CDC interface. */
#define USB_HID_CHANNEL_STATUS_CDC_OPEN (1u << 4)
/** @brief Per-channel health bit: the matching UART uses PIO rather than hardware UART. */
#define USB_HID_CHANNEL_STATUS_PIO_BACKEND (1u << 5)
/** @brief Per-channel health bit: UART RX data has been overwritten since boot. */
#define USB_HID_CHANNEL_STATUS_RX_OVERRUN (1u << 6)
/** @brief Per-channel health bit: the UART observed a receive-status error since boot. */
#define USB_HID_CHANNEL_STATUS_RX_ERROR (1u << 7)

/**
 * @brief Compact traffic and queue snapshot for one CDC/UART channel.
 */
typedef struct {
    uint8_t health; /**< UART status flags plus @ref USB_HID_CHANNEL_STATUS_* flags. */
    uint8_t ring_high_watermark_blocks; /**< Largest RX or TX ring occupancy in 16-byte blocks. */
    uint16_t controller_tx_bytes; /**< Saturated controller TX byte delta since the preceding report. */
    uint16_t controller_rx_bytes; /**< Saturated controller RX byte delta since the preceding report. */
    uint16_t cdc_tx_bytes; /**< Saturated CDC-to-host byte delta since the preceding report. */
    uint16_t cdc_rx_bytes; /**< Saturated host-to-CDC byte delta since the preceding report. */
} __attribute__((packed)) usb_hid_channel_status_t;

/**
 * @brief Compact HID monitor report published to the host.
 *
 * Layout is 63 bytes so TinyUSB can prepend Report ID 1 inside a 64-byte FS EP.
 */
typedef struct {
    uint8_t signature0; /**< Fixed report signature byte (`P`). */
    uint8_t version; /**< Report layout version. */
    uint8_t sequence; /**< Monotonic report sequence number. */
    usb_hid_channel_status_t channel[UART_PORT_COUNT]; /**< Per-CDC/UART bridge snapshots. */
} __attribute__((packed)) usb_hid_status_report_t;

_Static_assert(sizeof(usb_hid_status_report_t) == 63u,
               "HID status report must be 63 bytes (Report ID + payload <= 64)");
_Static_assert(sizeof(usb_hid_status_report_t) + 1u <= PICO_UART_USB_HID_ENDPOINT_BUFFER_SIZE,
               "HID status Report ID + payload must fit the HID EP buffer");

/**
 * @brief HID feature report containing temperature and firmware version.
 */
typedef struct {
    uint8_t version; /**< Report layout version. */
    uint8_t reserved0; /**< Reserved for board-status flags. */
    int16_t temperature_centidegrees_celsius; /**< Internal temperature in hundredths of a degree Celsius. */
    uint8_t firmware_major; /**< Firmware semantic version major component. */
    uint8_t firmware_minor; /**< Firmware semantic version minor component. */
    uint8_t firmware_patch; /**< Firmware semantic version patch component. */
    uint8_t reserved1; /**< Reserved; always zero. */
} __attribute__((packed)) usb_hid_board_status_report_t;

_Static_assert(sizeof(usb_hid_board_status_report_t) == 8u,
               "HID board-status report must match the HID report descriptor");

/** @brief Next absolute time, in milliseconds, when a HID report may be published. */
static uint32_t usb_hid_next_report_ms;
/** @brief Sequence number inserted into HID reports. */
static uint8_t usb_hid_sequence;
/** @brief UART counters captured when the last periodic HID input report was published. */
static uart_driver_port_stats_t usb_hid_last_reported_uart_stats[UART_PORT_COUNT];
/** @brief Most recent coherent UART counter snapshot for each port. */
static uart_driver_port_stats_t usb_hid_last_sampled_uart_stats[UART_PORT_COUNT];
/** @brief CDC counters captured when the last periodic HID input report was published. */
static usb_cdc_port_stats_t usb_hid_last_reported_cdc_stats[UART_PORT_COUNT];
/** @brief Deadline after which a previously armed HID reset expires. */
static absolute_time_t usb_hid_reset_armed_deadline;

static uint16_t usb_hid_clamp_u16(uint32_t value)
{
    return (value > (uint32_t)UINT16_MAX) ? (uint16_t)UINT16_MAX : (uint16_t)value;
}

static uint8_t usb_hid_clamp_u8(uint32_t value)
{
    return (value > (uint32_t)UINT8_MAX) ? (uint8_t)UINT8_MAX : (uint8_t)value;
}

static void usb_hid_sample_stats(uart_driver_port_stats_t uart_stats[UART_PORT_COUNT],
                                 usb_cdc_port_stats_t cdc_stats[UART_PORT_COUNT])
{
    memset(cdc_stats, 0, sizeof(usb_cdc_port_stats_t) * UART_PORT_COUNT);

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_stats[index] = usb_hid_last_sampled_uart_stats[index];
        if (uart_driver_port_stats((uart_port_id_t)index, &uart_stats[index])) {
            usb_hid_last_sampled_uart_stats[index] = uart_stats[index];
        } else {
            uart_stats[index] = usb_hid_last_sampled_uart_stats[index];
        }
        (void)usb_cdc_port_stats((uint8_t)index, &cdc_stats[index]);
    }
}

/**
 * @brief Snapshot the board-scoped status exposed through HID feature report 3.
 * @param report Destination feature-report payload.
 */
static void usb_hid_build_board_status_report(usb_hid_board_status_report_t *report)
{
    memset(report, 0, sizeof(*report));
    report->version = USB_HID_REPORT_VERSION;
    report->temperature_centidegrees_celsius = (int16_t)(temperature_read_celsius() * 100.0f);
    report->firmware_major = (uint8_t)PICO_UART_VERSION_MAJOR;
    report->firmware_minor = (uint8_t)PICO_UART_VERSION_MINOR;
    report->firmware_patch = (uint8_t)PICO_UART_VERSION_PATCH;
}

static void usb_hid_build_status_report(
    usb_hid_status_report_t *report,
    const uart_driver_port_stats_t uart_stats[UART_PORT_COUNT],
    const usb_cdc_port_stats_t cdc_stats[UART_PORT_COUNT])
{
    memset(report, 0, sizeof(*report));
    report->signature0 = USB_HID_SIGNATURE0;
    report->version = USB_HID_REPORT_VERSION;
    report->sequence = usb_hid_sequence;

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        const uart_driver_port_info_t *port_info = uart_driver_port_info((uart_port_id_t)index);
        uint32_t ring_high_watermark;

        if (port_info == NULL) {
            continue;
        }

        report->channel[index].health = uart_driver_port_status((uart_port_id_t)index);
        if (cdc_stats[index].opened) {
            report->channel[index].health |= USB_HID_CHANNEL_STATUS_CDC_OPEN;
        }
        if (port_info->backend == UART_DRIVER_BACKEND_PIO) {
            report->channel[index].health |= USB_HID_CHANNEL_STATUS_PIO_BACKEND;
        }
        if ((uart_stats[index].rx_ring_overflow_count != 0u) ||
            (uart_stats[index].rx_ring_pending_overflow_count != 0u)) {
            report->channel[index].health |= USB_HID_CHANNEL_STATUS_RX_OVERRUN;
        }
        if (uart_stats[index].rx_error_count != 0u) {
            report->channel[index].health |= USB_HID_CHANNEL_STATUS_RX_ERROR;
        }
        ring_high_watermark = uart_stats[index].tx_ring_high_watermark;
        if (uart_stats[index].rx_ring_high_watermark > ring_high_watermark) {
            ring_high_watermark = uart_stats[index].rx_ring_high_watermark;
        }
        report->channel[index].ring_high_watermark_blocks = usb_hid_clamp_u8(
            (ring_high_watermark + 15u) / 16u);
        report->channel[index].controller_tx_bytes = usb_hid_clamp_u16(
            uart_stats[index].controller_tx_bytes -
            usb_hid_last_reported_uart_stats[index].controller_tx_bytes);
        report->channel[index].controller_rx_bytes = usb_hid_clamp_u16(
            uart_stats[index].controller_rx_bytes -
            usb_hid_last_reported_uart_stats[index].controller_rx_bytes);
        report->channel[index].cdc_tx_bytes = usb_hid_clamp_u16(
            cdc_stats[index].tx_bytes - usb_hid_last_reported_cdc_stats[index].tx_bytes);
        report->channel[index].cdc_rx_bytes = usb_hid_clamp_u16(
            cdc_stats[index].rx_bytes - usb_hid_last_reported_cdc_stats[index].rx_bytes);
    }
}

static bool usb_hid_publish_status_report(void)
{
    uart_driver_port_stats_t uart_stats[UART_PORT_COUNT];
    usb_cdc_port_stats_t cdc_stats[UART_PORT_COUNT];
    usb_hid_status_report_t report;

    usb_hid_sample_stats(uart_stats, cdc_stats);
    usb_hid_build_status_report(&report, uart_stats, cdc_stats);
    if (!tud_hid_report(USB_HID_REPORT_ID_STATUS, &report, sizeof(report))) {
        return false;
    }

    memcpy(usb_hid_last_reported_uart_stats, uart_stats, sizeof(usb_hid_last_reported_uart_stats));
    memcpy(usb_hid_last_reported_cdc_stats, cdc_stats, sizeof(usb_hid_last_reported_cdc_stats));
    usb_hid_sequence += 1u;
    return true;
}

void usb_hid_init(void)
{
    usb_hid_sequence = 0u;
    usb_hid_next_report_ms = to_ms_since_boot(get_absolute_time());
    usb_hid_reset_armed_deadline = nil_time;
    memset(usb_hid_last_reported_uart_stats, 0, sizeof(usb_hid_last_reported_uart_stats));
    memset(usb_hid_last_sampled_uart_stats, 0, sizeof(usb_hid_last_sampled_uart_stats));
    memset(usb_hid_last_reported_cdc_stats, 0, sizeof(usb_hid_last_reported_cdc_stats));
}

void usb_hid_poll(void)
{
    uint32_t now_ms;

    if (!tud_hid_ready()) {
        return;
    }

    now_ms = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now_ms - usb_hid_next_report_ms) < 0) {
        return;
    }

    if (!usb_hid_publish_status_report()) {
        return;
    }

    usb_hid_next_report_ms = now_ms + USB_HID_STATUS_INTERVAL_MS;
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen)
{
    usb_hid_board_status_report_t board_status_report;
    usb_hid_status_report_t report;
    uart_driver_port_stats_t uart_stats[UART_PORT_COUNT];
    usb_cdc_port_stats_t cdc_stats[UART_PORT_COUNT];

    (void)instance;

    if ((report_type == HID_REPORT_TYPE_FEATURE) && (report_id == USB_HID_REPORT_ID_BOARD_STATUS)) {
        usb_hid_build_board_status_report(&board_status_report);
        if (reqlen > sizeof(board_status_report)) {
            reqlen = sizeof(board_status_report);
        }

        memcpy(buffer, &board_status_report, reqlen);
        return reqlen;
    }

    if ((report_type != HID_REPORT_TYPE_INPUT) || (report_id != USB_HID_REPORT_ID_STATUS)) {
        return 0u;
    }

    usb_hid_sample_stats(uart_stats, cdc_stats);
    usb_hid_build_status_report(&report, uart_stats, cdc_stats);
    if (reqlen > sizeof(report)) {
        reqlen = sizeof(report);
    }

    memcpy(buffer, &report, reqlen);
    return reqlen;
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize)
{
    (void)instance;

    if ((report_id != USB_HID_REPORT_ID_COMMAND) ||
        (report_type != HID_REPORT_TYPE_FEATURE) ||
        (bufsize < 1u)) {
        return;
    }

    switch (buffer[0]) {
    case USB_HID_COMMAND_TOGGLE_LED:
        led_toggle();
        break;

    case USB_HID_COMMAND_ARM_RESET:
#if PICO_UART_ALLOW_HID_RESET
        usb_hid_reset_armed_deadline = make_timeout_time_ms(USB_HID_RESET_ARM_WINDOW_MS);
#endif
        break;

    case USB_HID_COMMAND_RESET_BOARD:
#if PICO_UART_ALLOW_HID_RESET
        if (!time_reached(usb_hid_reset_armed_deadline)) {
            usb_hid_reset_armed_deadline = nil_time;
            system_reset();
        }
#endif
        break;

    default:
        break;
    }
}