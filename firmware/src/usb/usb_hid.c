/**
 * @file usb_hid.c
 * @brief TinyUSB HID monitor helpers for PicoUart status reporting.
 */

#include "usb/usb_hid.h"

#include "driver/led.h"
#include "driver/system.h"
#include "driver/temperature.h"
#include "uart/uart_driver.h"

#include "pico/stdlib.h"
#include "tusb.h"

#include <limits.h>
#include <string.h>

/** @brief HID status report interval in milliseconds. */
#define USB_HID_STATUS_INTERVAL_MS 100u
/** @brief HID status report signature byte 0. */
#define USB_HID_SIGNATURE0 'P'
/** @brief HID status report signature byte 1. */
#define USB_HID_SIGNATURE1 'U'
/** @brief HID status report format version. */
#define USB_HID_REPORT_VERSION 11u
/** @brief HID input report ID for the compact status monitor. */
#define USB_HID_REPORT_ID_STATUS 1u
/** @brief HID feature report ID for full-width PIO statistics. */
#define USB_HID_REPORT_ID_PIO_STATS 2u
/** @brief HID feature report ID for board temperature. */
#define USB_HID_REPORT_ID_BOARD_STATUS 3u
/** @brief HID feature report ID for board control commands. */
#define USB_HID_REPORT_ID_COMMAND 4u

/** @brief HID command value that toggles the board LED. */
#define USB_HID_COMMAND_TOGGLE_LED 1u
/** @brief HID command value that immediately resets the board. */
#define USB_HID_COMMAND_RESET_BOARD 2u

/** @brief Number of PIO-backed logical UART ports exposed by the firmware. */
#define USB_HID_PIO_PORT_COUNT 4u

/**
 * @brief Compact HID monitor report published to the host.
 */
typedef struct {
    uint8_t signature0; /**< Fixed report signature byte 0. */
    uint8_t signature1; /**< Fixed report signature byte 1. */
    uint8_t version; /**< Report layout version. */
    uint8_t port_count; /**< Number of logical UART ports. */
    uint8_t sequence; /**< Monotonic report sequence number. */
    uint8_t worker_state; /**< Worker-core state flags. */
    uint8_t last_command_status; /**< Latest command result with temporary HardFault marker in bits 4-5. */
    uint8_t last_command_port; /**< Temporary full-width worker heartbeat diagnostic. */
    uint8_t backend[UART_PORT_COUNT]; /**< Backend type for each port. */
    uint8_t tx_pin[UART_PORT_COUNT]; /**< TX GPIO assignment for each port. */
    uint8_t rx_pin[UART_PORT_COUNT]; /**< RX GPIO assignment for each port. */
    uint8_t status[UART_PORT_COUNT]; /**< Per-port status flags. */
    uint16_t pio_rx_framing_error_count[USB_HID_PIO_PORT_COUNT]; /**< Per-report delta of per-port PIO RX framing errors. */
    uint16_t pio_tx_dma_claim_failure_count[USB_HID_PIO_PORT_COUNT]; /**< Per-report delta of per-port PIO TX DMA claim failures. */
    uint16_t pio_tx_polled_bytes[USB_HID_PIO_PORT_COUNT]; /**< Per-report delta of per-port PIO TX poll-path bytes. */
    uint16_t pio_tx_dma_bytes[USB_HID_PIO_PORT_COUNT]; /**< Per-report delta of per-port PIO TX DMA-path bytes. */
} usb_hid_status_report_t;

/**
 * @brief Full-width HID feature report containing 32-bit PIO counters.
 */
typedef struct {
    uint32_t rx_framing_error_count[USB_HID_PIO_PORT_COUNT]; /**< Full per-port PIO RX framing-error counters. */
    uint32_t tx_dma_claim_failure_count[USB_HID_PIO_PORT_COUNT]; /**< Full per-port failed TX DMA claim counters. */
    uint32_t tx_polled_bytes[USB_HID_PIO_PORT_COUNT]; /**< Full per-port TX poll-path byte counters. */
    uint32_t tx_dma_bytes[USB_HID_PIO_PORT_COUNT]; /**< Full per-port TX DMA-path byte counters. */
} usb_hid_pio_stats_report_t;

/**
 * @brief HID feature report containing the internal temperature estimate.
 */
typedef struct {
    uint8_t version; /**< Report layout version. */
    uint8_t reserved; /**< Reserved for board-status flags. */
    int16_t temperature_centidegrees_celsius; /**< Internal temperature in hundredths of a degree Celsius. */
} usb_hid_board_status_report_t;

/** @brief HID worker state flag: UART worker core has started. */
#define USB_HID_WORKER_STATE_RUNNING (1u << 0)
/** @brief HID worker state flag: current UART transport scope is 8N1-only. */
#define USB_HID_WORKER_STATE_8N1_ONLY (1u << 1)
/** @brief Bit offset of the worker's current poll port in the status byte. */
#define USB_HID_WORKER_STATE_POLL_PORT_SHIFT 2u
/** @brief Bit offset of the worker poll-loop heartbeat in the status byte. */
#define USB_HID_WORKER_STATE_HEARTBEAT_SHIFT 5u
/** @brief Number of heartbeat bits carried by the status report. */
#define USB_HID_WORKER_STATE_HEARTBEAT_MASK 0x07u

/** @brief Next absolute time, in milliseconds, when a HID report may be published. */
static uint32_t usb_hid_next_report_ms;
/** @brief Sequence number inserted into HID reports. */
static uint8_t usb_hid_sequence;
/** @brief Full-width PIO counters captured when the last periodic HID input report was published. */
static uart_driver_pio_stats_t usb_hid_last_reported_pio_stats[USB_HID_PIO_PORT_COUNT];

static uint16_t usb_hid_clamp_u16(uint32_t value)
{
    return (value > (uint32_t)UINT16_MAX) ? (uint16_t)UINT16_MAX : (uint16_t)value;
}

static void usb_hid_sample_pio_stats(uart_driver_pio_stats_t stats[USB_HID_PIO_PORT_COUNT])
{
    memset(stats, 0, sizeof(uart_driver_pio_stats_t) * USB_HID_PIO_PORT_COUNT);

    for (size_t index = UART_PORT_2; index < UART_PORT_COUNT; ++index) {
        (void)uart_driver_port_pio_stats((uart_port_id_t)index, &stats[index - UART_PORT_2]);
    }
}

static void usb_hid_build_pio_stats_report(usb_hid_pio_stats_report_t *report)
{
    memset(report, 0, sizeof(*report));

    for (size_t index = UART_PORT_2; index < UART_PORT_COUNT; ++index) {
        uart_driver_pio_stats_t stats;
        size_t pio_index = index - UART_PORT_2;

        if (!uart_driver_port_pio_stats((uart_port_id_t)index, &stats)) {
            continue;
        }

        report->rx_framing_error_count[pio_index] = stats.rx_framing_error_count;
        report->tx_dma_claim_failure_count[pio_index] = stats.tx_dma_claim_failure_count;
        report->tx_polled_bytes[pio_index] = stats.tx_polled_bytes;
        report->tx_dma_bytes[pio_index] = stats.tx_dma_bytes;
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
}

static void usb_hid_build_status_report(usb_hid_status_report_t *report,
                                        const uart_driver_pio_stats_t current_stats[USB_HID_PIO_PORT_COUNT])
{
    memset(report, 0, sizeof(*report));
    report->signature0 = USB_HID_SIGNATURE0;
    report->signature1 = USB_HID_SIGNATURE1;
    report->version = USB_HID_REPORT_VERSION;
    report->port_count = (uint8_t)uart_driver_port_count();
    report->sequence = usb_hid_sequence;
    report->worker_state = USB_HID_WORKER_STATE_8N1_ONLY;
    if (uart_driver_worker_is_running()) {
        report->worker_state |= USB_HID_WORKER_STATE_RUNNING;
    }
    report->worker_state |= (uint8_t)(uart_driver_worker_poll_port()
                                      << USB_HID_WORKER_STATE_POLL_PORT_SHIFT);
    report->worker_state |= (uint8_t)((uart_driver_worker_heartbeat() &
                                       USB_HID_WORKER_STATE_HEARTBEAT_MASK)
                                      << USB_HID_WORKER_STATE_HEARTBEAT_SHIFT);
    report->last_command_status = (uint8_t)uart_driver_last_command_status();
    report->last_command_status |= (uint8_t)(uart_driver_hardfault_core() << 4u);
    report->last_command_port = uart_driver_worker_heartbeat();

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        const uart_driver_port_info_t *port_info = uart_driver_port_info((uart_port_id_t)index);

        if (port_info == NULL) {
            continue;
        }

        report->backend[index] = (uint8_t)port_info->backend;
        report->tx_pin[index] = (uint8_t)port_info->tx_pin;
        report->rx_pin[index] = (uint8_t)port_info->rx_pin;
        report->status[index] = uart_driver_port_status((uart_port_id_t)index);

        if ((port_info->backend == UART_DRIVER_BACKEND_PIO) &&
            (index >= UART_PORT_2) &&
            ((index - UART_PORT_2) < USB_HID_PIO_PORT_COUNT)) {
            size_t pio_index = index - UART_PORT_2;

            report->pio_rx_framing_error_count[pio_index] = usb_hid_clamp_u16(
                current_stats[pio_index].rx_framing_error_count -
                usb_hid_last_reported_pio_stats[pio_index].rx_framing_error_count);
            report->pio_tx_dma_claim_failure_count[pio_index] = usb_hid_clamp_u16(
                current_stats[pio_index].tx_dma_claim_failure_count -
                usb_hid_last_reported_pio_stats[pio_index].tx_dma_claim_failure_count);
            report->pio_tx_polled_bytes[pio_index] = usb_hid_clamp_u16(
                current_stats[pio_index].tx_polled_bytes -
                usb_hid_last_reported_pio_stats[pio_index].tx_polled_bytes);
            report->pio_tx_dma_bytes[pio_index] = usb_hid_clamp_u16(
                current_stats[pio_index].tx_dma_bytes -
                usb_hid_last_reported_pio_stats[pio_index].tx_dma_bytes);
        }
    }
}

static bool usb_hid_publish_status_report(void)
{
    uart_driver_pio_stats_t current_stats[USB_HID_PIO_PORT_COUNT];
    usb_hid_status_report_t report;

    usb_hid_sample_pio_stats(current_stats);
    usb_hid_build_status_report(&report, current_stats);
    if (!tud_hid_report(USB_HID_REPORT_ID_STATUS, &report, sizeof(report))) {
        return false;
    }

    memcpy(usb_hid_last_reported_pio_stats,
           current_stats,
           sizeof(usb_hid_last_reported_pio_stats));
    usb_hid_sequence += 1u;
    return true;
}

void usb_hid_init(void)
{
    usb_hid_sequence = 0u;
    usb_hid_next_report_ms = to_ms_since_boot(get_absolute_time());
    memset(usb_hid_last_reported_pio_stats, 0, sizeof(usb_hid_last_reported_pio_stats));
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
    usb_hid_pio_stats_report_t stats_report;
    usb_hid_board_status_report_t board_status_report;
    usb_hid_status_report_t report;
    uart_driver_pio_stats_t current_stats[USB_HID_PIO_PORT_COUNT];

    (void)instance;

    if ((report_type == HID_REPORT_TYPE_FEATURE) && (report_id == USB_HID_REPORT_ID_PIO_STATS)) {
        usb_hid_build_pio_stats_report(&stats_report);
        if (reqlen > sizeof(stats_report)) {
            reqlen = sizeof(stats_report);
        }

        memcpy(buffer, &stats_report, reqlen);
        return reqlen;
    }

    if ((report_type == HID_REPORT_TYPE_FEATURE) && (report_id == USB_HID_REPORT_ID_BOARD_STATUS)) {
        usb_hid_build_board_status_report(&board_status_report);
        if (reqlen > sizeof(board_status_report)) {
            reqlen = sizeof(board_status_report);
        }

        memcpy(buffer, &board_status_report, reqlen);
        return reqlen;
    }

    usb_hid_sample_pio_stats(current_stats);
    usb_hid_build_status_report(&report, current_stats);
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

    case USB_HID_COMMAND_RESET_BOARD:
        system_reset();
        break;

    default:
        break;
    }
}