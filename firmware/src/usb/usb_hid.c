/**
 * @file usb_hid.c
 * @brief Version-1 HID identity, board control, telemetry, and channel snapshots.
 */

#include "usb/usb_hid.h"

#include "config/capacity_config.h"
#include "config/usb_identity.h"
#include "platform/boot_config.h"
#include "platform/led.h"
#include "platform/service_load.h"
#include "platform/system.h"
#include "platform/temperature.h"
#include "uart/uart_driver.h"
#include "uart/saturating.h"
#include "usb/usb_cdc.h"

#include "hardware/clocks.h"
#include "hardware/regs/addressmap.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"

#include <limits.h>
#include <stddef.h>
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
#ifndef PICO_UART_ALLOW_HID_RESET
#define PICO_UART_ALLOW_HID_RESET 1
#endif

#define USB_HID_REPORT_VERSION 1u
#define USB_HID_REPORT_ID_DEVICE_INFO 1u
#define USB_HID_REPORT_ID_CONTROL 2u
#define USB_HID_REPORT_ID_TELEMETRY 3u
#define USB_HID_REPORT_ID_CHANNEL_BASE 10u
#define USB_HID_COMMAND_LED_OFF 1u
#define USB_HID_COMMAND_LED_ON 2u
#define USB_HID_COMMAND_ARM_REBOOT 3u
#define USB_HID_COMMAND_REBOOT 4u
#define USB_HID_COMMAND_SAVE_CONFIGURATION 5u
#define USB_HID_CHANNEL_INTERVAL_MS 500u
#define USB_HID_FAULT_SAMPLE_INTERVAL_MS 100u
#define USB_HID_RESET_ARM_WINDOW_MS 2000u
#define USB_HID_REBOOT_DELAY_MS 1000u

#define USB_HID_DEVICE_INFO_SIZE 63u
#define USB_HID_CONTROL_SIZE 24u
#define USB_HID_TELEMETRY_SIZE 24u
#define USB_HID_CHANNEL_SIZE 54u
#define USB_HID_CHANNEL_STATUS_READY (1u << 0)
#define USB_HID_CHANNEL_STATUS_INIT_FAILED (1u << 1)
#define USB_HID_CHANNEL_STATUS_CONTROL_ERROR (1u << 2)
#define USB_HID_CHANNEL_STATUS_CONTROL_PENDING (1u << 3)
#define USB_HID_CHANNEL_STATUS_CDC_OPEN (1u << 4)
#define USB_HID_CHANNEL_STATUS_PIO_BACKEND (1u << 5)
#define USB_HID_CHANNEL_STATUS_RX_OVERRUN (1u << 6)
#define USB_HID_CHANNEL_STATUS_RX_ERROR (1u << 7)

_Static_assert(UART_PORT_COUNT == 6u, "HID version 1 requires six UART channels");
_Static_assert(USB_HID_DEVICE_INFO_SIZE + 1u <= PICO_UART_USB_HID_ENDPOINT_BUFFER_SIZE,
               "Device information report must fit one HID packet");
_Static_assert(USB_HID_CHANNEL_SIZE + 1u <= PICO_UART_USB_HID_ENDPOINT_BUFFER_SIZE,
               "Channel report must fit one HID packet");

/** @brief Command result state published in report 2. */
typedef enum {
    USB_HID_STATUS_IDLE = 0,        /**< No command has produced a result. */
    USB_HID_STATUS_PENDING = 1,     /**< Save accepted and awaiting core-0 execution. */
    USB_HID_STATUS_COMPLETED = 2,   /**< Action completed or reboot is scheduled. */
    USB_HID_STATUS_INVALID = 3,     /**< Request rejected with an invalid argument. */
    USB_HID_STATUS_UNSUPPORTED = 4, /**< Command or required capability is unavailable. */
    USB_HID_STATUS_BUSY = 5,        /**< Save rejected because a CDC session is open. */
    USB_HID_STATUS_FAILED = 6,      /**< Save execution or verification failed. */
} usb_hid_command_status_t;

/** @brief Protocol error reason associated with report 2 status. */
typedef enum {
    USB_HID_ERROR_NONE = 0,             /**< No error. */
    USB_HID_ERROR_MALFORMED = 1,        /**< Framing, flags, or reserved fields are invalid. */
    USB_HID_ERROR_PROFILE = 2,          /**< Profile is invalid or unsupported by this target. */
    USB_HID_ERROR_ACKNOWLEDGEMENT = 3,  /**< Required overclock acknowledgement is missing. */
    USB_HID_ERROR_AUTHORIZATION = 4,    /**< Standalone reboot is not armed. */
    USB_HID_ERROR_UNSUPPORTED = 5,      /**< Command or capability is unavailable. */
    USB_HID_ERROR_CDC_OPEN = 6,         /**< A CDC DTR session is open. */
    USB_HID_ERROR_FLASH = 7,            /**< Flash operation failed. */
    USB_HID_ERROR_VERIFY = 8,            /**< Flash verification failed. */
    USB_HID_ERROR_EXECUTION = 9,        /**< Other execution failure. */
} usb_hid_error_t;

/** @brief One saturating fault category; all categories share a timestamp. */
typedef struct {
    uint32_t count; /**< Saturating cumulative observations for the category. */
} usb_hid_fault_count_t;

/** @brief USB-core-owned sticky history for the four channel fault categories. */
typedef struct {
    uint8_t health; /**< Last observed driver and historical RX status flags. */
    usb_hid_fault_count_t faults[4]; /**< Init, control, RX-overrun, and RX-error counts. */
    uint32_t last_fault_uptime_s; /**< Shared uptime timestamp, rounded down to seconds. */
    uint32_t rx_counts[2]; /**< Last sampled backend overrun and error counters. */
} usb_hid_fault_history_t;

/** @brief Boot-lifetime traffic accumulation harvested from wrapping backend counters. */
typedef struct {
    uint32_t previous[4]; /**< Last raw UART-TX, UART-RX, CDC-TX, and CDC-RX counters. */
    uint64_t total[4]; /**< Monotonic totals since MCU boot. */
    uint64_t last_publication_ms; /**< Measurement boundary for the report interval. */
    uint16_t sequence; /**< Last accepted per-channel Input sequence. */
    bool initialized; /**< Raw counter baseline has been established. */
} usb_hid_channel_traffic_t;

/** @brief Last identifiable control request, retained for request-ID deduplication. */
typedef struct {
    uint8_t payload[USB_HID_CONTROL_SIZE]; /**< Exact latest identifiable request bytes. */
    bool valid; /**< True after one identifiable request has been processed. */
} usb_hid_request_cache_t;

static usb_hid_command_status_t usb_hid_command_status;
static uint8_t usb_hid_command;
static uint8_t usb_hid_command_flags;
static uint32_t usb_hid_request_id;
static uint32_t usb_hid_requested_profile;
static uint8_t usb_hid_error_reason;
static usb_hid_request_cache_t usb_hid_latest_request;
static absolute_time_t usb_hid_arm_deadline;
static absolute_time_t usb_hid_reboot_deadline;
static bool usb_hid_reboot_pending;
static usb_hid_channel_traffic_t usb_hid_traffic[UART_PORT_COUNT];
static usb_hid_fault_history_t usb_hid_faults[UART_PORT_COUNT];
static uart_driver_port_stats_t usb_hid_last_uart_stats[UART_PORT_COUNT];
static usb_cdc_port_stats_t usb_hid_last_cdc_stats[UART_PORT_COUNT];
static uint8_t usb_hid_next_channel;
static uint32_t usb_hid_next_fault_sample_ms;

/** @brief Read a little-endian 32-bit value from a report payload. */
static uint32_t usb_hid_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

/** @brief Write a little-endian 16-bit value to a report payload. */
static void usb_hid_write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

/** @brief Write a little-endian 32-bit value to a report payload. */
static void usb_hid_write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

/** @brief Saturating 64-bit addition for long-lived traffic accumulators. */
static uint64_t usb_hid_add_u64(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

/** @brief Return a saturated 32-bit wire representation. */
static uint32_t usb_hid_saturate_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

/** @brief Require every DTR session to be closed before accepting or executing a save. */
static bool usb_hid_cdc_sessions_closed(void)
{
    for (uint8_t index = 0u; index < UART_PORT_COUNT; ++index) {
        usb_cdc_port_stats_t stats;
        if (!usb_cdc_port_stats(index, &stats) || stats.opened) {
            return false;
        }
    }
    return true;
}

/** @brief Total statically-addressed SRAM reserved by the current linked image. */
static uint32_t usb_hid_ram_reserved_bytes(void)
{
    extern char __end__, __scratch_x_start__, __scratch_x_end__, __scratch_y_start__, __scratch_y_end__;
    extern char __StackBottom, __StackTop, __StackOneBottom, __StackOneTop;

    return (uint32_t)(((uintptr_t)&__end__ - SRAM_BASE) +
                      ((uintptr_t)&__scratch_x_end__ - (uintptr_t)&__scratch_x_start__) +
                      ((uintptr_t)&__scratch_y_end__ - (uintptr_t)&__scratch_y_start__) +
                      ((uintptr_t)&__StackTop - (uintptr_t)&__StackBottom) +
                      ((uintptr_t)&__StackOneTop - (uintptr_t)&__StackOneBottom));
}

/** @brief Sample backend/CDC counters and retain zero defaults on transient read failure. */
static void usb_hid_read_stats(uart_driver_port_stats_t uart[UART_PORT_COUNT],
                               usb_cdc_port_stats_t cdc[UART_PORT_COUNT])
{
    for (size_t channel = 0u; channel < UART_PORT_COUNT; ++channel) {
        uart[channel] = usb_hid_last_uart_stats[channel];
        if (uart_driver_port_stats((uart_port_id_t)channel, &uart[channel])) {
            usb_hid_last_uart_stats[channel] = uart[channel];
        }
        cdc[channel] = usb_hid_last_cdc_stats[channel];
        if (usb_cdc_port_stats((uint8_t)channel, &cdc[channel])) {
            usb_hid_last_cdc_stats[channel] = cdc[channel];
        }
    }
}

/** @brief Harvest raw byte counters frequently so their 32-bit wrap cannot erase boot-lifetime totals. */
static void usb_hid_harvest_traffic(const uart_driver_port_stats_t uart[UART_PORT_COUNT],
                                   const usb_cdc_port_stats_t cdc[UART_PORT_COUNT])
{
    for (size_t channel = 0u; channel < UART_PORT_COUNT; ++channel) {
        usb_hid_channel_traffic_t *traffic = &usb_hid_traffic[channel];
        const uint32_t raw[4] = {uart[channel].uart_tx_bytes, uart[channel].uart_rx_bytes,
                                 cdc[channel].tx_bytes, cdc[channel].rx_bytes};
        if (!traffic->initialized) {
            memcpy(traffic->previous, raw, sizeof(raw));
            traffic->initialized = true;
            continue;
        }
        for (size_t counter = 0u; counter < 4u; ++counter) {
            /* ponytail: One 32-bit wrap between USB-core polls is impossible at UART line rates; if polling can stall
             * for hours, move harvesting into a wider backend accumulator. */
            traffic->total[counter] =
                usb_hid_add_u64(traffic->total[counter], (uint32_t)(raw[counter] - traffic->previous[counter]));
            traffic->previous[counter] = raw[counter];
        }
    }
}

/** @brief Observe sticky fault transitions and backend counter increases on the USB core. */
static void usb_hid_sample_faults(size_t channel, const uart_driver_port_stats_t *stats)
{
    usb_hid_fault_history_t *history = &usb_hid_faults[channel];
    const uint8_t health = uart_driver_port_status((uart_port_id_t)channel);
    const uint32_t counters[2] = {
        uart_saturating_add_u32(stats->rx_ring_overflow_count, stats->rx_ring_pending_overflow_count),
        stats->rx_error_count,
    };
    const uint64_t uptime_s = time_us_64() / UINT64_C(1000000);
    bool changed = false;

    for (size_t fault = 0u; fault < 2u; ++fault) {
        const uint8_t mask = (uint8_t)(1u << (fault + 1u));
        if ((health & mask) != 0u && (history->health & mask) == 0u) {
            history->faults[fault].count = uart_saturating_add_u32(history->faults[fault].count, 1u);
            changed = true;
        }
    }
    for (size_t counter = 0u; counter < 2u; ++counter) {
        const uint32_t increase = counters[counter] >= history->rx_counts[counter]
                                      ? counters[counter] - history->rx_counts[counter]
                                      : counters[counter];
        if (increase != 0u) {
            history->faults[counter + 2u].count =
                uart_saturating_add_u32(history->faults[counter + 2u].count, increase);
            changed = true;
        }
        history->rx_counts[counter] = counters[counter];
    }
    history->health = (uint8_t)((health & 0x3fu) |
                                ((history->faults[2].count != 0u) ? USB_HID_CHANNEL_STATUS_RX_OVERRUN : 0u) |
                                ((history->faults[3].count != 0u) ? USB_HID_CHANNEL_STATUS_RX_ERROR : 0u));
    if (changed) {
        history->last_fault_uptime_s = uptime_s > UINT32_MAX ? UINT32_MAX : (uint32_t)uptime_s;
    }
}

/** @brief Start a USB publication session, optionally initializing boot-lifetime totals. */
static void usb_hid_start_traffic_session(bool boot_start)
{
    uart_driver_port_stats_t uart[UART_PORT_COUNT] = {0};
    usb_cdc_port_stats_t cdc[UART_PORT_COUNT] = {0};
    const uint64_t now_ms = time_us_64() / 1000u;

    usb_hid_read_stats(uart, cdc);
    if (!boot_start) {
        usb_hid_harvest_traffic(uart, cdc);
    }
    for (size_t channel = 0u; channel < UART_PORT_COUNT; ++channel) {
        usb_hid_channel_traffic_t *traffic = &usb_hid_traffic[channel];
        if (boot_start) {
            memset(traffic, 0, sizeof(*traffic));
            traffic->total[0] = uart[channel].uart_tx_bytes;
            traffic->total[1] = uart[channel].uart_rx_bytes;
            traffic->total[2] = cdc[channel].tx_bytes;
            traffic->total[3] = cdc[channel].rx_bytes;
        }
        traffic->previous[0] = uart[channel].uart_tx_bytes;
        traffic->previous[1] = uart[channel].uart_rx_bytes;
        traffic->previous[2] = cdc[channel].tx_bytes;
        traffic->previous[3] = cdc[channel].rx_bytes;
        traffic->last_publication_ms = now_ms;
        traffic->sequence = 0u;
        traffic->initialized = true;
    }
    usb_hid_next_channel = 0u;
}

/** @brief Build the static 63-byte identity/capabilities payload. */
static void usb_hid_build_device_info(uint8_t payload[USB_HID_DEVICE_INFO_SIZE])
{
    bool led_on = false;
    char serial[2u * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1u];
    uint16_t capabilities = 0u;

    memset(payload, 0, USB_HID_DEVICE_INFO_SIZE);
    payload[0] = USB_HID_REPORT_VERSION;
    memcpy(payload + 1u, PICO_UART_USB_PRODUCT, sizeof(PICO_UART_USB_PRODUCT));
    pico_get_unique_board_id_string(serial, sizeof(serial));
    memcpy(payload + 33u, serial, 16u);
    payload[49] = (uint8_t)PICO_UART_VERSION_MAJOR;
    payload[50] = (uint8_t)PICO_UART_VERSION_MINOR;
    payload[51] = (uint8_t)PICO_UART_VERSION_PATCH;
#if PICO_RP2350
    payload[52] = 2u;
    payload[58] = 0x1fu;
#else
    payload[52] = 1u;
    payload[58] = 0x0fu;
#endif
    payload[53] = UART_PORT_COUNT;
    payload[54] = 0x03u;
    payload[55] = 0x3cu;
    if (led_read_state(&led_on)) {
        capabilities |= 1u << 0;
    }
#if PICO_UART_ALLOW_HID_RESET
    capabilities |= 1u << 1;
#endif
    capabilities |= 1u << 2;
    usb_hid_write_u16(payload + 56u, capabilities);
    usb_hid_write_u32(payload + 59u, SRAM_END - SRAM_BASE);
}

/** @brief Build the current 24-byte command result and boot-configuration snapshot. */
static void usb_hid_build_control(uint8_t payload[USB_HID_CONTROL_SIZE])
{
    const boot_config_status_t *config = boot_config_get_status();

    memset(payload, 0, USB_HID_CONTROL_SIZE);
    payload[0] = USB_HID_REPORT_VERSION;
    payload[1] = usb_hid_command;
    payload[2] = usb_hid_command_flags;
    payload[3] = (uint8_t)usb_hid_command_status;
    usb_hid_write_u32(payload + 4u, usb_hid_request_id);
    usb_hid_write_u32(payload + 8u, config->stored_profile);
    usb_hid_write_u32(payload + 12u, (uint32_t)config->active_profile);
    usb_hid_write_u32(payload + 16u, usb_hid_requested_profile);
    payload[20] = (uint8_t)config->source;
    payload[21] = (uint8_t)config->reason;
    payload[22] = usb_hid_error_reason;
}

/** @brief Build live board telemetry using the report-3 wire offsets. */
static void usb_hid_build_telemetry(uint8_t payload[USB_HID_TELEMETRY_SIZE])
{
    bool led_on = false;
    const bool led_available = led_read_state(&led_on);
    const int16_t temperature = temperature_to_hid_centidegrees(temperature_read_celsius());
    const uint64_t uptime = time_us_64() / 1000u;

    memset(payload, 0, USB_HID_TELEMETRY_SIZE);
    payload[0] = USB_HID_REPORT_VERSION;
    payload[1] = led_available && led_on ? 1u : 0u;
    usb_hid_write_u16(payload + 2u, (uint16_t)temperature);
    for (size_t byte = 0u; byte < 8u; ++byte) {
        payload[4u + byte] = (uint8_t)(uptime >> (8u * byte));
    }
    usb_hid_write_u16(payload + 12u, service_load_read(0u));
    usb_hid_write_u16(payload + 14u, service_load_read(1u));
    usb_hid_write_u32(payload + 16u, usb_hid_ram_reserved_bytes());
    usb_hid_write_u32(payload + 20u, clock_get_hz(clk_sys));
}

/** @brief Capture and encode a coherent per-channel 54-byte snapshot. */
static void usb_hid_build_channel(uint8_t channel, uint8_t payload[USB_HID_CHANNEL_SIZE], bool candidate,
                                  const uart_driver_port_stats_t *stats, const usb_cdc_port_stats_t *cdc,
                                  uint64_t now_ms)
{
    usb_hid_channel_traffic_t *traffic = &usb_hid_traffic[channel];
    usb_hid_fault_history_t *history = &usb_hid_faults[channel];
    uart_driver_port_info_t info = {.tx_pin = UART_DRIVER_PIN_UNASSIGNED, .rx_pin = UART_DRIVER_PIN_UNASSIGNED};
    uint8_t driver_status = 0u;
    const uint32_t ring_high_watermark = stats->tx_ring_high_watermark > stats->rx_ring_high_watermark
                                             ? stats->tx_ring_high_watermark
                                             : stats->rx_ring_high_watermark;
    const uint64_t elapsed = now_ms >= traffic->last_publication_ms ? now_ms - traffic->last_publication_ms : 0u;

    (void)uart_driver_port_snapshot((uart_port_id_t)channel, &info, &driver_status);
    memset(payload, 0, USB_HID_CHANNEL_SIZE);
    payload[0] = USB_HID_REPORT_VERSION;
    usb_hid_write_u16(payload + 1u, candidate ? (uint16_t)(traffic->sequence + 1u) : traffic->sequence);
    usb_hid_write_u32(payload + 3u, usb_hid_saturate_u32(elapsed));
    payload[7] = (uint8_t)((driver_status & 0x0fu) | (cdc->opened ? USB_HID_CHANNEL_STATUS_CDC_OPEN : 0u) |
                           ((info.backend == UART_DRIVER_BACKEND_PIO) ? USB_HID_CHANNEL_STATUS_PIO_BACKEND : 0u) |
                           (history->health & 0xc0u));
    payload[8] = (uint8_t)((ring_high_watermark + 15u) / 16u);
    for (size_t counter = 0u; counter < 4u; ++counter) {
        usb_hid_write_u32(payload + 9u + (4u * counter), (uint32_t)traffic->total[counter]);
    }
    if (info.baud_rate != 0u) {
        usb_hid_write_u32(payload + 25u, info.baud_rate);
        payload[29] = info.data_bits;
        payload[30] = info.stop_bits;
        payload[31] = (uint8_t)info.parity;
    }
    payload[32] = info.tx_pin <= UINT8_MAX ? (uint8_t)info.tx_pin : UINT8_MAX;
    payload[33] = info.rx_pin <= UINT8_MAX ? (uint8_t)info.rx_pin : UINT8_MAX;
    for (size_t fault = 0u; fault < 4u; ++fault) {
        usb_hid_write_u32(payload + 34u + fault * 4u, history->faults[fault].count);
    }
    usb_hid_write_u32(payload + 50u, history->last_fault_uptime_s);
}

/** @brief Publish one due channel snapshot and commit its interval only on TinyUSB acceptance. */
static bool usb_hid_publish_channel(uint8_t channel, const uart_driver_port_stats_t *stats,
                                    const usb_cdc_port_stats_t *cdc, uint64_t now_ms)
{
    uint8_t payload[USB_HID_CHANNEL_SIZE];
    usb_hid_channel_traffic_t *traffic = &usb_hid_traffic[channel];

    usb_hid_build_channel(channel, payload, true, stats, cdc, now_ms);
    if (!tud_hid_report((uint8_t)(USB_HID_REPORT_ID_CHANNEL_BASE + channel), payload, sizeof(payload))) {
        return false;
    }
    service_load_note(0u);
    traffic->last_publication_ms = now_ms;
    traffic->sequence = (uint16_t)(traffic->sequence + 1u);
    usb_hid_next_channel = (uint8_t)((channel + 1u) % UART_PORT_COUNT);
    return true;
}

/** @brief Store common result metadata for an identifiable command. */
static void usb_hid_set_result(uint8_t command, uint8_t flags, uint32_t request_id, uint32_t profile,
                               usb_hid_command_status_t status, usb_hid_error_t error)
{
    usb_hid_command = command;
    usb_hid_command_flags = flags;
    usb_hid_request_id = request_id;
    usb_hid_requested_profile = profile;
    usb_hid_command_status = status;
    usb_hid_error_reason = (uint8_t)error;
}

/** @brief Decode and validate a report-2 SET request in the specified contract order. */
static void usb_hid_handle_command(const uint8_t *request, uint16_t length)
{
    const bool identifiable = length == USB_HID_CONTROL_SIZE && request != NULL &&
                              request[0] == USB_HID_REPORT_VERSION && usb_hid_read_u32(request + 4u) != 0u;
    const uint8_t command = identifiable ? request[1] : 0u;
    const uint8_t flags = identifiable ? request[2] : 0u;
    const uint32_t request_id = identifiable ? usb_hid_read_u32(request + 4u) : 0u;
    uint32_t profile = BOOT_CONFIG_PROFILE_UNAVAILABLE;

    if (usb_hid_command_status == USB_HID_STATUS_PENDING || usb_hid_reboot_pending) {
        return;
    }
    if (identifiable && usb_hid_latest_request.valid &&
        request_id == usb_hid_read_u32(usb_hid_latest_request.payload + 4u)) {
        return;
    }
    if (!identifiable) {
        usb_hid_latest_request.valid = false;
        usb_hid_set_result(0u, 0u, 0u, BOOT_CONFIG_PROFILE_UNAVAILABLE, USB_HID_STATUS_INVALID,
                           USB_HID_ERROR_MALFORMED);
        return;
    }

    memcpy(usb_hid_latest_request.payload, request, USB_HID_CONTROL_SIZE);
    usb_hid_latest_request.valid = true;
    if ((request[3] != 0u) || (usb_hid_read_u32(request + 8u) != 0u) ||
        (usb_hid_read_u32(request + 12u) != 0u) || (request[20] != 0u) || (request[21] != 0u) ||
        (request[22] != 0u) || (request[23] != 0u) || (flags & 0xfeu) != 0u) {
        usb_hid_set_result(command, flags, request_id, profile, USB_HID_STATUS_INVALID, USB_HID_ERROR_MALFORMED);
        return;
    }

    bool led_on = false;
    const bool led_available = led_read_state(&led_on);
    switch (command) {
    case USB_HID_COMMAND_LED_OFF:
    case USB_HID_COMMAND_LED_ON:
        if (!led_available) {
            usb_hid_set_result(command, flags, request_id, profile, USB_HID_STATUS_UNSUPPORTED,
                               USB_HID_ERROR_UNSUPPORTED);
            return;
        }
        break;
    case USB_HID_COMMAND_ARM_REBOOT:
    case USB_HID_COMMAND_REBOOT:
#if !PICO_UART_ALLOW_HID_RESET
        usb_hid_set_result(command, flags, request_id, profile, USB_HID_STATUS_UNSUPPORTED,
                           USB_HID_ERROR_UNSUPPORTED);
        return;
#endif
        break;
    case USB_HID_COMMAND_SAVE_CONFIGURATION:
        break;
    default:
        usb_hid_set_result(command, flags, request_id, profile, USB_HID_STATUS_UNSUPPORTED,
                           USB_HID_ERROR_UNSUPPORTED);
        return;
    }

    if (command == USB_HID_COMMAND_SAVE_CONFIGURATION) {
        profile = usb_hid_read_u32(request + 16u);
        if (!boot_config_profile_supported(profile)) {
            usb_hid_set_result(command, flags, request_id, BOOT_CONFIG_PROFILE_UNAVAILABLE, USB_HID_STATUS_INVALID,
                               USB_HID_ERROR_PROFILE);
            return;
        }
    } else if (flags != 0u || usb_hid_read_u32(request + 16u) != 0u) {
        usb_hid_set_result(command, flags, request_id, BOOT_CONFIG_PROFILE_UNAVAILABLE, USB_HID_STATUS_INVALID,
                           USB_HID_ERROR_MALFORMED);
        return;
    }

    if (command == USB_HID_COMMAND_SAVE_CONFIGURATION && profile != BOOT_CLOCK_DEFAULT && (flags & 1u) == 0u) {
        usb_hid_set_result(command, flags, request_id, profile, USB_HID_STATUS_INVALID,
                           USB_HID_ERROR_ACKNOWLEDGEMENT);
        return;
    }
    if (command == USB_HID_COMMAND_REBOOT) {
#if PICO_UART_ALLOW_HID_RESET
        if (usb_hid_arm_deadline == nil_time || time_reached(usb_hid_arm_deadline)) {
            usb_hid_arm_deadline = nil_time;
            usb_hid_set_result(command, flags, request_id, BOOT_CONFIG_PROFILE_UNAVAILABLE,
                               USB_HID_STATUS_INVALID, USB_HID_ERROR_AUTHORIZATION);
            return;
        }
#endif
    }
    if (command == USB_HID_COMMAND_SAVE_CONFIGURATION && !usb_hid_cdc_sessions_closed()) {
        usb_hid_set_result(command, flags, request_id, profile, USB_HID_STATUS_BUSY, USB_HID_ERROR_CDC_OPEN);
        return;
    }

    usb_hid_set_result(command, flags, request_id, profile, USB_HID_STATUS_COMPLETED, USB_HID_ERROR_NONE);
    switch (command) {
    case USB_HID_COMMAND_LED_OFF:
        led_set(false);
        break;
    case USB_HID_COMMAND_LED_ON:
        led_set(true);
        break;
    case USB_HID_COMMAND_ARM_REBOOT:
#if PICO_UART_ALLOW_HID_RESET
        usb_hid_arm_deadline = make_timeout_time_ms(USB_HID_RESET_ARM_WINDOW_MS);
#endif
        break;
    case USB_HID_COMMAND_REBOOT:
#if PICO_UART_ALLOW_HID_RESET
        usb_hid_arm_deadline = nil_time;
        usb_hid_reboot_pending = true;
        usb_hid_reboot_deadline = make_timeout_time_ms(USB_HID_REBOOT_DELAY_MS);
#endif
        break;
    case USB_HID_COMMAND_SAVE_CONFIGURATION:
        if (!usb_hid_cdc_sessions_closed()) {
            usb_hid_set_result(command, flags, request_id, profile, USB_HID_STATUS_BUSY, USB_HID_ERROR_CDC_OPEN);
        } else {
            usb_hid_set_result(command, flags, request_id, profile, USB_HID_STATUS_PENDING, USB_HID_ERROR_NONE);
        }
        break;
    default:
        break;
    }
}

/** @brief Execute the deferred save outside the TinyUSB control callback and schedule reboot on success. */
static void usb_hid_execute_pending_save(void)
{
    const boot_config_t config = {.clock_profile = (boot_clock_profile_t)usb_hid_requested_profile};
    if (!usb_hid_cdc_sessions_closed()) {
        usb_hid_command_status = USB_HID_STATUS_BUSY;
        usb_hid_error_reason = USB_HID_ERROR_CDC_OPEN;
    } else if (!boot_config_set(&config)) {
        usb_hid_command_status = USB_HID_STATUS_FAILED;
        usb_hid_error_reason = USB_HID_ERROR_EXECUTION;
    } else {
        const boot_config_save_result_t save_result = boot_config_save_with_result();
        if (save_result == BOOT_CONFIG_SAVE_FLASH_FAILED) {
            usb_hid_command_status = USB_HID_STATUS_FAILED;
            usb_hid_error_reason = USB_HID_ERROR_FLASH;
        } else if (save_result == BOOT_CONFIG_SAVE_VERIFY_FAILED) {
            usb_hid_command_status = USB_HID_STATUS_FAILED;
            usb_hid_error_reason = USB_HID_ERROR_VERIFY;
        } else {
            usb_hid_command_status = USB_HID_STATUS_COMPLETED;
            usb_hid_error_reason = USB_HID_ERROR_NONE;
            usb_hid_reboot_pending = true;
            usb_hid_reboot_deadline = make_timeout_time_ms(USB_HID_REBOOT_DELAY_MS);
        }
    }
}

void usb_hid_init(void)
{
    usb_hid_command_status = USB_HID_STATUS_IDLE;
    usb_hid_command = 0u;
    usb_hid_command_flags = 0u;
    usb_hid_request_id = 0u;
    usb_hid_requested_profile = BOOT_CONFIG_PROFILE_UNAVAILABLE;
    usb_hid_error_reason = USB_HID_ERROR_NONE;
    memset(&usb_hid_latest_request, 0, sizeof(usb_hid_latest_request));
    memset(usb_hid_faults, 0, sizeof(usb_hid_faults));
    memset(usb_hid_last_uart_stats, 0, sizeof(usb_hid_last_uart_stats));
    memset(usb_hid_last_cdc_stats, 0, sizeof(usb_hid_last_cdc_stats));
    memset(usb_hid_traffic, 0, sizeof(usb_hid_traffic));
    usb_hid_arm_deadline = nil_time;
    usb_hid_reboot_deadline = nil_time;
    usb_hid_reboot_pending = false;
    usb_hid_next_fault_sample_ms = 0u;
    usb_hid_start_traffic_session(true);
}

void usb_hid_poll(void)
{
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    uart_driver_port_stats_t uart[UART_PORT_COUNT] = {0};
    usb_cdc_port_stats_t cdc[UART_PORT_COUNT] = {0};

    if (usb_hid_command_status == USB_HID_STATUS_PENDING) {
        usb_hid_execute_pending_save();
    }
    if (usb_hid_reboot_pending && time_reached(usb_hid_reboot_deadline)) {
        system_reset();
    }

    usb_hid_read_stats(uart, cdc);
    usb_hid_harvest_traffic(uart, cdc);
    if ((int32_t)(now_ms - usb_hid_next_fault_sample_ms) >= 0) {
        for (size_t channel = 0u; channel < UART_PORT_COUNT; ++channel) {
            usb_hid_sample_faults(channel, &uart[channel]);
        }
        usb_hid_next_fault_sample_ms = now_ms + USB_HID_FAULT_SAMPLE_INTERVAL_MS;
    }

    if (!tud_hid_ready()) {
        return;
    }
    const uint64_t now_ms_64 = time_us_64() / 1000u;
    for (size_t offset = 0u; offset < UART_PORT_COUNT; ++offset) {
        const uint8_t channel = (uint8_t)((usb_hid_next_channel + offset) % UART_PORT_COUNT);
        if (now_ms_64 - usb_hid_traffic[channel].last_publication_ms < USB_HID_CHANNEL_INTERVAL_MS) {
            continue;
        }
        if (!usb_hid_publish_channel(channel, &uart[channel], &cdc[channel], now_ms_64)) {
            usb_hid_next_channel = (uint8_t)((channel + 1u) % UART_PORT_COUNT);
        }
        return;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    uint8_t payload[USB_HID_DEVICE_INFO_SIZE];
    uint16_t length;
    (void)instance;

    if (buffer == NULL) {
        return 0u;
    }
    if (report_type == HID_REPORT_TYPE_FEATURE && report_id == USB_HID_REPORT_ID_DEVICE_INFO) {
        usb_hid_build_device_info(payload);
        length = USB_HID_DEVICE_INFO_SIZE;
    } else if (report_type == HID_REPORT_TYPE_FEATURE && report_id == USB_HID_REPORT_ID_CONTROL) {
        usb_hid_build_control(payload);
        length = USB_HID_CONTROL_SIZE;
    } else if (report_type == HID_REPORT_TYPE_FEATURE && report_id == USB_HID_REPORT_ID_TELEMETRY) {
        usb_hid_build_telemetry(payload);
        length = USB_HID_TELEMETRY_SIZE;
    } else if ((report_type == HID_REPORT_TYPE_FEATURE || report_type == HID_REPORT_TYPE_INPUT) &&
               report_id >= USB_HID_REPORT_ID_CHANNEL_BASE &&
               report_id < USB_HID_REPORT_ID_CHANNEL_BASE + UART_PORT_COUNT) {
        const uint8_t channel = (uint8_t)(report_id - USB_HID_REPORT_ID_CHANNEL_BASE);
        uart_driver_port_stats_t uart[UART_PORT_COUNT] = {0};
        usb_cdc_port_stats_t cdc[UART_PORT_COUNT] = {0};
        usb_hid_read_stats(uart, cdc);
        usb_hid_harvest_traffic(uart, cdc);
        usb_hid_sample_faults(channel, &uart[channel]);
        usb_hid_build_channel(channel, payload, false, &uart[channel], &cdc[channel], time_us_64() / 1000u);
        length = USB_HID_CHANNEL_SIZE;
    } else {
        return 0u;
    }

    if (reqlen > length) {
        reqlen = length;
    }
    memcpy(buffer, payload, reqlen);
    return reqlen;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    (void)instance;
    if (report_type == HID_REPORT_TYPE_FEATURE && report_id == USB_HID_REPORT_ID_CONTROL) {
        usb_hid_handle_command(buffer, bufsize);
    }
}

void usb_hid_reset_host_state(void)
{
    usb_hid_arm_deadline = nil_time;
    usb_hid_start_traffic_session(false);
}
