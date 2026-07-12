/**
 * @file usb_hid.c
 * @brief TinyUSB HID monitor helpers for PicoUart status reporting.
 */

#include "usb/usb_hid.h"

#include "driver/uart_driver.h"

#include "pico/stdlib.h"
#include "tusb.h"

#include <string.h>

/** @brief HID status report interval in milliseconds. */
#define USB_HID_STATUS_INTERVAL_MS 100u
/** @brief HID status report signature byte 0. */
#define USB_HID_SIGNATURE0 'P'
/** @brief HID status report signature byte 1. */
#define USB_HID_SIGNATURE1 'U'
/** @brief HID status report format version. */
#define USB_HID_REPORT_VERSION 1u

/**
 * @brief Compact HID monitor report published to the host.
 */
typedef struct {
    uint8_t signature0; /**< Fixed report signature byte 0. */
    uint8_t signature1; /**< Fixed report signature byte 1. */
    uint8_t version; /**< Report layout version. */
    uint8_t port_count; /**< Number of logical UART ports. */
    uint8_t sequence; /**< Monotonic report sequence number. */
    uint8_t reserved[3]; /**< Reserved bytes for future status fields. */
    uint8_t backend[UART_PORT_COUNT]; /**< Backend type for each port. */
    uint8_t tx_pin[UART_PORT_COUNT]; /**< TX GPIO assignment for each port. */
    uint8_t rx_pin[UART_PORT_COUNT]; /**< RX GPIO assignment for each port. */
    uint8_t status[UART_PORT_COUNT]; /**< Per-port status flags. */
} usb_hid_status_report_t;

/** @brief Next absolute time, in milliseconds, when a HID report may be published. */
static uint32_t usb_hid_next_report_ms;
/** @brief Sequence number inserted into HID reports. */
static uint8_t usb_hid_sequence;

static void usb_hid_build_status_report(usb_hid_status_report_t *report)
{
    memset(report, 0, sizeof(*report));
    report->signature0 = USB_HID_SIGNATURE0;
    report->signature1 = USB_HID_SIGNATURE1;
    report->version = USB_HID_REPORT_VERSION;
    report->port_count = (uint8_t)uart_driver_port_count();
    report->sequence = usb_hid_sequence;

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        const uart_driver_port_info_t *port_info = uart_driver_port_info((uart_port_id_t)index);

        if (port_info == NULL) {
            continue;
        }

        report->backend[index] = (uint8_t)port_info->backend;
        report->tx_pin[index] = (uint8_t)port_info->tx_pin;
        report->rx_pin[index] = (uint8_t)port_info->rx_pin;
        report->status[index] = 0u;
    }
}

void usb_hid_init(void)
{
    usb_hid_sequence = 0u;
    usb_hid_next_report_ms = to_ms_since_boot(get_absolute_time());
}

void usb_hid_poll(void)
{
    usb_hid_status_report_t report;
    uint32_t now_ms;

    if (!tud_hid_ready()) {
        return;
    }

    now_ms = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now_ms - usb_hid_next_report_ms) < 0) {
        return;
    }

    usb_hid_build_status_report(&report);
    if (!tud_hid_report(0u, &report, sizeof(report))) {
        return;
    }

    usb_hid_sequence += 1u;
    usb_hid_next_report_ms = now_ms + USB_HID_STATUS_INTERVAL_MS;
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen)
{
    usb_hid_status_report_t report;

    (void)instance;
    (void)report_id;
    (void)report_type;

    usb_hid_build_status_report(&report);
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
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}