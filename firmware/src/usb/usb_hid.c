/**
 * @file usb_hid.c
 * @brief HID identity, configuration, telemetry, board actions, and channel monitoring.
 */

#include "usb/usb_hid.h"
#include "config/capacity_config.h"
#include "platform/boot_config.h"
#include "platform/led.h"
#include "platform/service_load.h"
#include "platform/system.h"
#include "platform/temperature.h"
#include "uart/saturating.h"
#include "uart/uart_driver.h"
#include "usb/usb_cdc.h"

#include "hardware/clocks.h"
#include "hardware/regs/addressmap.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
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
#ifndef PICO_UART_ALLOW_HID_RESET
#define PICO_UART_ALLOW_HID_RESET 1
#endif
#ifndef PICO_UART_BOARD_MODEL
#define PICO_UART_BOARD_MODEL 0u
#endif

#define USB_HID_REPORT_VERSION 1u
#define USB_HID_REPORT_ID_DEVICE_INFO 1u
#define USB_HID_REPORT_ID_CONFIGURATION 2u
#define USB_HID_REPORT_ID_TELEMETRY 3u
#define USB_HID_REPORT_ID_CHANNEL_BASE 10u
#define USB_HID_REPORT_ID_CONTROL 20u
#define USB_HID_REPORT_ID_DIAGNOSTIC_BASE 30u
#define USB_HID_COMMAND_LED_OFF 1u
#define USB_HID_COMMAND_LED_ON 2u
#define USB_HID_COMMAND_REBOOT 3u
#define USB_HID_COMMAND_SAVE_CONFIGURATION 4u
#define USB_HID_CHANNEL_INTERVAL_MS 500u
#define USB_HID_FAULT_SAMPLE_INTERVAL_MS 100u
#define USB_HID_REBOOT_DELAY_MS 1000u
#define USB_HID_DEVICE_INFO_SIZE 27u
#define USB_HID_CONFIGURATION_SIZE 36u
#define USB_HID_TELEMETRY_SIZE 20u
#define USB_HID_CHANNEL_SIZE 30u
#define USB_HID_CONTROL_SIZE 4u
#define USB_HID_DIAGNOSTIC_SIZE 24u

_Static_assert(UART_PORT_COUNT == 6u, "HID configuration requires six UART channels");
_Static_assert(PICO_UNIQUE_BOARD_ID_SIZE_BYTES == 8u, "HID requires an eight-byte SDK board identity");
_Static_assert(USB_HID_CONFIGURATION_SIZE + 1u <= PICO_UART_USB_HID_ENDPOINT_BUFFER_SIZE,
               "Largest HID report must fit one packet");
_Static_assert(PICO_UART_HW_UART_TX_BUFFER_SIZE == PICO_UART_PIO_UART_TX_BUFFER_SIZE &&
               PICO_UART_HW_UART_RX_BUFFER_SIZE == PICO_UART_PIO_UART_RX_BUFFER_SIZE,
               "HID configuration requires shared per-channel capacities");
_Static_assert(PICO_UART_HW_UART_TX_BUFFER_SIZE <= UINT16_MAX &&
               PICO_UART_HW_UART_RX_BUFFER_SIZE <= UINT16_MAX &&
               PICO_UART_USB_CDC_TX_BUFFER_SIZE <= UINT16_MAX &&
               PICO_UART_USB_CDC_RX_BUFFER_SIZE <= UINT16_MAX, "HID capacities must fit uint16");

/** @brief Device-global action result, retained until another request or reboot. */
typedef enum {
    USB_HID_STATUS_IDLE = 0,        /**< No result yet. */
    USB_HID_STATUS_PENDING = 1,     /**< Deferred save awaiting execution. */
    USB_HID_STATUS_COMPLETED = 2,   /**< Action completed or reboot scheduled. */
    USB_HID_STATUS_INVALID = 3,     /**< Invalid framing, profile, or intent flags. */
    USB_HID_STATUS_UNSUPPORTED = 4, /**< Action or capability unavailable. */
    USB_HID_STATUS_BUSY = 5,        /**< Save blocked by an open CDC session. */
    USB_HID_STATUS_FAILED = 6,      /**< Execution or verification failed. */
} usb_hid_command_status_t;

/** @brief Error values paired with action status in report 20. */
typedef enum {
    USB_HID_ERROR_NONE = 0,            /**< No error. */
    USB_HID_ERROR_MALFORMED = 1,       /**< Length, version, flags, or unused argument invalid. */
    USB_HID_ERROR_PROFILE = 2,         /**< Unknown or unadvertised profile. */
    USB_HID_ERROR_ACKNOWLEDGEMENT = 3, /**< Missing non-default clock acknowledgement. */
    USB_HID_ERROR_AUTHORIZATION = 4,   /**< Missing destructive-action confirmation. */
    USB_HID_ERROR_UNSUPPORTED = 5,     /**< Action or capability unavailable. */
    USB_HID_ERROR_CDC_OPEN = 6,        /**< Save requires all CDC sessions closed. */
    USB_HID_ERROR_FLASH = 7,           /**< Flash operation failed. */
    USB_HID_ERROR_VERIFY = 8,          /**< Flash verification failed. */
    USB_HID_ERROR_EXECUTION = 9,       /**< Other execution failure. */
} usb_hid_error_t;

/** @brief Validity of the last UART statistics acquisition, independent of lifecycle. */
typedef enum {
    USB_HID_MEASUREMENT_UNAVAILABLE = 0, /**< No accepted UART sample since boot. */
    USB_HID_MEASUREMENT_CURRENT = 1,     /**< Acquired coherently for this capture. */
    USB_HID_MEASUREMENT_CACHED = 2,      /**< Reusing the last accepted sample. */
} usb_hid_measurement_t;

/** @brief USB-core-owned lifetime accounting and publication state for one channel. */
typedef struct {
    uart_driver_port_stats_t uart; /**< Last accepted UART statistics; never populated by a failed attempt. */
    usb_cdc_port_stats_t cdc; /**< Latest USB-core-owned CDC statistics. */
    uint64_t uart_sample_us; /**< Acquisition time of the last accepted UART sample. */
    usb_hid_measurement_t measurement; /**< Acquisition outcome for this capture. */
    uint32_t previous[4]; /**< Raw UART TX/RX and CDC TX/RX accounting baselines. */
    uint64_t total[4]; /**< Boot-lifetime handoff totals, independent of Input commits. */
    uint32_t rx_previous[2]; /**< Raw RX overrun/error baselines. */
    uint32_t rx_count[2]; /**< Saturating observed RX fault history. */
    uint16_t peaks[4]; /**< Lifetime observed UART TX/RX and CDC TX/RX peaks in bytes. */
    uart_driver_control_history_t control; /**< Latest independently captured control-error history. */
    uint32_t last_fault_s; /**< Monotonic merged RX/control fault observation timestamp. */
    uint64_t last_publication_ms; /**< Last accepted Input interval boundary. */
    uint16_t sequence; /**< Last accepted Input sequence, wrapping modulo 65536. */
} usb_hid_channel_t;

static usb_hid_channel_t usb_hid_channels[UART_PORT_COUNT];
static uint8_t usb_hid_identity[USB_HID_DEVICE_INFO_SIZE];
static uint8_t usb_hid_configuration[USB_HID_CONFIGURATION_SIZE];
static usb_hid_command_status_t usb_hid_command_status;
static uint8_t usb_hid_command;
static uint8_t usb_hid_requested_profile;
static uint8_t usb_hid_error_reason;
static absolute_time_t usb_hid_reboot_deadline;
static bool usb_hid_reboot_pending;
static uint8_t usb_hid_next_channel;
static uint32_t usb_hid_next_fault_sample_ms;
/** @brief EP0 SET storage, owned by the serialized HID control-transfer adapter. */
static uint8_t usb_hid_control_buffer[PICO_UART_USB_HID_ENDPOINT_BUFFER_SIZE];

/** @brief Write an unaligned little-endian integer. */
static void usb_hid_write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

/** @brief Write an unaligned little-endian integer. */
static void usb_hid_write_u32(uint8_t *bytes, uint32_t value)
{
    for (size_t byte = 0u; byte < 4u; ++byte) {
        bytes[byte] = (uint8_t)(value >> (8u * byte));
    }
}

/** @brief Merge a fault timestamp without allowing an older observation to replace it. */
static void usb_hid_note_fault(usb_hid_channel_t *channel, uint64_t observation_us)
{
    uint64_t seconds = observation_us / UINT64_C(1000000);
    uint32_t value = seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
    if (value > channel->last_fault_s) {
        channel->last_fault_s = value;
    }
}

/** @brief Capture one channel, accounting accepted observations exactly once on the USB core. */
static bool usb_hid_capture(uint8_t index)
{
    usb_hid_channel_t *channel = &usb_hid_channels[index];
    uart_driver_port_stats_t candidate;

    if (uart_driver_port_stats((uart_port_id_t)index, &candidate)) {
        const uint64_t now_us = time_us_64();
        channel->uart = candidate;
        channel->uart_sample_us = now_us;
        channel->measurement = USB_HID_MEASUREMENT_CURRENT;
        const uint32_t rx[2] = {
            uart_saturating_add_u32(candidate.rx_ring_overflow_count, candidate.rx_ring_pending_overflow_count),
            candidate.rx_error_count,
        };
        for (size_t counter = 0u; counter < 2u; ++counter) {
            uint32_t delta = rx[counter] >= channel->rx_previous[counter] ?
                rx[counter] - channel->rx_previous[counter] : rx[counter];
            channel->rx_previous[counter] = rx[counter];
            channel->rx_count[counter] = uart_saturating_add_u32(channel->rx_count[counter], delta);
            if (delta != 0u) {
                usb_hid_note_fault(channel, now_us);
            }
        }
    } else if (channel->measurement != USB_HID_MEASUREMENT_UNAVAILABLE) {
        channel->measurement = USB_HID_MEASUREMENT_CACHED;
    }

    if (!usb_cdc_port_stats(index, &channel->cdc) ||
        !uart_driver_control_history((uart_port_id_t)index, &channel->control)) {
        return false;
    }
    if (channel->control.reason != UART_CONTROL_ERROR_NONE) {
        usb_hid_note_fault(channel, channel->control.uptime_us);
    }
    const uint32_t raw[4] = {channel->uart.uart_tx_bytes, channel->uart.uart_rx_bytes,
                             channel->cdc.tx_bytes, channel->cdc.rx_bytes};
    const uint16_t peaks[4] = {channel->uart.tx_ring_high_watermark, channel->uart.rx_ring_high_watermark,
                               channel->cdc.tx_fifo_high_watermark, channel->cdc.rx_fifo_high_watermark};
    for (size_t counter = 0u; counter < 4u; ++counter) {
        /* ponytail: At most one raw wrap between polls is sufficient at UART rates; multi-hour USB-core stalls
         * would require backend-owned wider counters, not a different HID publication baseline. */
        channel->total[counter] += (uint32_t)(raw[counter] - channel->previous[counter]);
        channel->previous[counter] = raw[counter];
        if (peaks[counter] > channel->peaks[counter]) {
            channel->peaks[counter] = peaks[counter];
        }
    }
    return true;
}

/** @brief Require all CDC DTR sessions closed before accepting or executing a save. */
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

/** @brief Calculate linked SRAM plus reserved SDK stacks, not runtime memory usage. */
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

/** @brief Capture immutable identity and boot configuration once, before enumeration. */
static void usb_hid_build_static_reports(void)
{
    pico_unique_board_id_t uid;
    bool led_on;
    pico_get_unique_board_id(&uid);
    usb_hid_identity[0] = USB_HID_REPORT_VERSION;
    usb_hid_write_u16(usb_hid_identity + 1u, 1u);
    memcpy(usb_hid_identity + 3u, uid.id, sizeof(uid.id));
    usb_hid_identity[11] = PICO_UART_VERSION_MAJOR;
    usb_hid_identity[12] = PICO_UART_VERSION_MINOR;
    usb_hid_identity[13] = PICO_UART_VERSION_PATCH;
#if PICO_RP2350
    usb_hid_identity[14] = 2u;
#else
    usb_hid_identity[14] = 1u;
#endif
    usb_hid_identity[15] = PICO_UART_BOARD_MODEL;
    usb_hid_identity[17] = (led_read_state(&led_on) ? 1u : 0u) |
                          (PICO_UART_ALLOW_HID_RESET ? 2u : 0u) | 4u;
    for (uint8_t profile = 0u; profile <= BOOT_CLOCK_280_MHZ; ++profile) {
        if (boot_config_profile_supported(profile)) {
            usb_hid_identity[18] |= (uint8_t)(1u << profile);
        }
    }
    usb_hid_write_u32(usb_hid_identity + 19u, SRAM_END - SRAM_BASE);
    usb_hid_write_u32(usb_hid_identity + 23u, usb_hid_ram_reserved_bytes());
    usb_hid_configuration[0] = USB_HID_REPORT_VERSION;
    uint32_t stored = boot_config_get_status()->stored_profile;
    usb_hid_configuration[1] = stored <= BOOT_CLOCK_280_MHZ ? (uint8_t)stored : UINT8_MAX;
    usb_hid_configuration[2] = UART_PORT_COUNT;
    for (uint8_t index = 0u; index < UART_PORT_COUNT; ++index) {
        const uart_driver_port_info_t *port = &uart_board_ports[index];
        const uint32_t pins[4] = {port->tx_pin, port->rx_pin, port->rts_pin, port->cts_pin};
        if (port->backend == UART_DRIVER_BACKEND_HW) {
            usb_hid_identity[16] |= (uint8_t)(1u << index);
        }
        for (size_t pin = 0u; pin < 4u; ++pin) {
            usb_hid_configuration[3u + index * 4u + pin] =
                pins[pin] <= UINT8_MAX ? (uint8_t)pins[pin] : UINT8_MAX;
        }
    }
    usb_hid_configuration[27] = system_reset_cause();
    usb_hid_write_u16(usb_hid_configuration + 28u, PICO_UART_HW_UART_TX_BUFFER_SIZE);
    usb_hid_write_u16(usb_hid_configuration + 30u, PICO_UART_HW_UART_RX_BUFFER_SIZE);
    usb_hid_write_u16(usb_hid_configuration + 32u, PICO_UART_USB_CDC_TX_BUFFER_SIZE);
    usb_hid_write_u16(usb_hid_configuration + 34u, PICO_UART_USB_CDC_RX_BUFFER_SIZE);
}

/** @brief Encode live telemetry only when all mandatory measurements are available. */
static bool usb_hid_build_telemetry(uint8_t *payload)
{
    bool led_on = false;
    bool available = led_read_state(&led_on);
    float temperature = temperature_read_celsius();
    uint32_t clock = clock_get_hz(clk_sys);
    if (!isfinite(temperature) || clock == 0u ||
        ((usb_hid_identity[17] & 1u) != 0u && !available)) {
        return false;
    }
    payload[0] = USB_HID_REPORT_VERSION;
    payload[1] = available && led_on ? 1u : 0u;
    usb_hid_write_u16(payload + 2u, (uint16_t)temperature_to_hid_centidegrees(temperature));
    uint64_t uptime = time_us_64() / 1000u;
    for (size_t byte = 0u; byte < 8u; ++byte) {
        payload[4u + byte] = (uint8_t)(uptime >> (byte * 8u));
    }
    usb_hid_write_u16(payload + 12u, service_load_read(0u));
    usb_hid_write_u16(payload + 14u, service_load_read(1u));
    usb_hid_write_u32(payload + 16u, clock);
    return true;
}

/** @brief Encode a coherent lifecycle/settings capture and independently valid UART totals. */
static bool usb_hid_build_channel(uint8_t index, uint8_t *payload, bool publication, uint64_t now_ms)
{
    usb_hid_channel_t *channel = &usb_hid_channels[index];
    uart_driver_port_info_t info;
    uint8_t status;
    if (!uart_driver_port_snapshot((uart_port_id_t)index, &info, &status)) {
        return false;
    }
    uint64_t elapsed = now_ms - channel->last_publication_ms;
    payload[0] = USB_HID_REPORT_VERSION;
    payload[1] = !channel->cdc.opened ? 0u :
        (status & (UART_DRIVER_PORT_STATUS_INIT_FAILED | UART_DRIVER_PORT_STATUS_CONTROL_ERROR)) ? 4u :
        !(status & UART_DRIVER_PORT_STATUS_READY) ? 1u :
        (status & UART_DRIVER_PORT_STATUS_CONTROL_PENDING) ? 3u : 2u;
    payload[2] = (uint8_t)channel->measurement;
    usb_hid_write_u32(payload + 3u, info.baud_rate);
    payload[7] = info.baud_rate != 0u ? info.data_bits : 0u;
    payload[8] = info.baud_rate != 0u ? info.stop_bits : 0u;
    payload[9] = info.baud_rate != 0u ? (uint8_t)info.parity : 0u;
    usb_hid_write_u16(payload + 10u, (uint16_t)(channel->sequence + (publication ? 1u : 0u)));
    usb_hid_write_u16(payload + 12u, elapsed > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed);
    for (size_t counter = 0u; counter < 4u; ++counter) {
        usb_hid_write_u32(payload + 14u + counter * 4u, (uint32_t)channel->total[counter]);
    }
    return true;
}

/** @brief Encode retained history and exact-byte observed peaks without consuming it. */
static void usb_hid_build_diagnostic(uint8_t index, uint8_t *payload)
{
    const usb_hid_channel_t *channel = &usb_hid_channels[index];
    uint64_t age = (time_us_64() - channel->uart_sample_us) / 100000u;
    payload[0] = USB_HID_REPORT_VERSION;
    payload[1] = (uint8_t)channel->measurement;
    payload[2] = channel->measurement == USB_HID_MEASUREMENT_CURRENT ? 0u :
        channel->measurement == USB_HID_MEASUREMENT_UNAVAILABLE || age > UINT8_MAX ? UINT8_MAX : (uint8_t)age;
    payload[3] = (uint8_t)channel->control.reason;
    usb_hid_write_u32(payload + 4u, channel->rx_count[0]);
    usb_hid_write_u32(payload + 8u, channel->rx_count[1]);
    usb_hid_write_u32(payload + 12u, channel->last_fault_s);
    for (size_t peak = 0u; peak < 4u; ++peak) {
        usb_hid_write_u16(payload + 16u + peak * 2u, channel->peaks[peak]);
    }
}

/** @brief Replace the terminal result before the control transfer's status acknowledgement. */
static void usb_hid_set_result(uint8_t action, usb_hid_command_status_t status, usb_hid_error_t error)
{
    usb_hid_command = action;
    usb_hid_command_status = status;
    usb_hid_error_reason = (uint8_t)error;
}

/** @brief Validate and execute/queue a report-20 request; false protects an in-flight result. */
static bool usb_hid_handle_command(const uint8_t *request, uint16_t length)
{
    if (usb_hid_command_status == USB_HID_STATUS_PENDING || usb_hid_reboot_pending) {
        return false;
    }
    bool framed = request != NULL && length == USB_HID_CONTROL_SIZE &&
                  request[0] == USB_HID_REPORT_VERSION;
    uint8_t action = framed && request[1] >= 1u && request[1] <= 4u ? request[1] : 0u;
    usb_hid_set_result(action, USB_HID_STATUS_INVALID, USB_HID_ERROR_MALFORMED);
    if (!framed || (request[2] & 0xfcu) != 0u) {
        return true;
    }
    uint8_t flags = request[2];
    uint8_t profile = request[3];
    uint8_t capability = action <= 2u ? 1u : action == 3u ? 2u : 4u;
    if (action == 0u || (usb_hid_identity[17] & capability) == 0u) {
        usb_hid_set_result(action, USB_HID_STATUS_UNSUPPORTED, USB_HID_ERROR_UNSUPPORTED);
        return true;
    }
    if ((action <= 2u && (flags != 0u || profile != 0u)) ||
        (action == 3u && (profile != 0u || (flags & 2u) != 0u)) ||
        (action == 4u && profile == 0u && (flags & 2u) != 0u)) {
        return true;
    }
    if (action == 4u && !boot_config_profile_supported(profile)) {
        usb_hid_set_result(action, USB_HID_STATUS_INVALID, USB_HID_ERROR_PROFILE);
        return true;
    }
    if (action >= 3u && (flags & 1u) == 0u) {
        usb_hid_set_result(action, USB_HID_STATUS_INVALID, USB_HID_ERROR_AUTHORIZATION);
        return true;
    }
    if (action == 4u && profile != 0u && (flags & 2u) == 0u) {
        usb_hid_set_result(action, USB_HID_STATUS_INVALID, USB_HID_ERROR_ACKNOWLEDGEMENT);
        return true;
    }
    if (action == 4u && !usb_hid_cdc_sessions_closed()) {
        usb_hid_set_result(action, USB_HID_STATUS_BUSY, USB_HID_ERROR_CDC_OPEN);
        return true;
    }
    usb_hid_set_result(action, USB_HID_STATUS_COMPLETED, USB_HID_ERROR_NONE);
    if (action <= 2u) {
        led_set(action == USB_HID_COMMAND_LED_ON);
    } else if (action == USB_HID_COMMAND_REBOOT) {
        usb_hid_reboot_pending = true;
        usb_hid_reboot_deadline = make_timeout_time_ms(USB_HID_REBOOT_DELAY_MS);
    } else {
        usb_hid_requested_profile = profile;
        usb_hid_command_status = USB_HID_STATUS_PENDING;
    }
    return true;
}

/** @brief Run flash-safe work outside the TinyUSB callback and schedule verified reboot. */
static void usb_hid_execute_pending_save(void)
{
    boot_config_t config = {.clock_profile = (boot_clock_profile_t)usb_hid_requested_profile};
    if (!usb_hid_cdc_sessions_closed()) {
        usb_hid_set_result(4u, USB_HID_STATUS_BUSY, USB_HID_ERROR_CDC_OPEN);
    } else if (!boot_config_set(&config)) {
        usb_hid_set_result(4u, USB_HID_STATUS_FAILED, USB_HID_ERROR_EXECUTION);
    } else {
        boot_config_save_result_t result = boot_config_save_with_result();
        if (result != BOOT_CONFIG_SAVE_OK) {
            usb_hid_set_result(4u, USB_HID_STATUS_FAILED,
                result == BOOT_CONFIG_SAVE_FLASH_FAILED ? USB_HID_ERROR_FLASH :
                result == BOOT_CONFIG_SAVE_VERIFY_FAILED ? USB_HID_ERROR_VERIFY : USB_HID_ERROR_EXECUTION);
        } else {
            usb_hid_set_result(4u, USB_HID_STATUS_COMPLETED, USB_HID_ERROR_NONE);
            usb_hid_reboot_pending = true;
            usb_hid_reboot_deadline = make_timeout_time_ms(USB_HID_REBOOT_DELAY_MS);
        }
    }
}

void usb_hid_init(void)
{
    memset(usb_hid_channels, 0, sizeof(usb_hid_channels));
    memset(usb_hid_identity, 0, sizeof(usb_hid_identity));
    memset(usb_hid_configuration, 0, sizeof(usb_hid_configuration));
    usb_hid_set_result(0u, USB_HID_STATUS_IDLE, USB_HID_ERROR_NONE);
    usb_hid_requested_profile = 0u;
    usb_hid_reboot_pending = false;
    usb_hid_next_fault_sample_ms = 0u;
    usb_hid_build_static_reports();
    usb_hid_reset_host_state();
}

void usb_hid_reset_host_state(void)
{
    for (uint8_t index = 0u; index < UART_PORT_COUNT; ++index) {
        usb_hid_channels[index].sequence = 0u;
        usb_hid_channels[index].last_publication_ms = time_us_64() / 1000u;
    }
    usb_hid_next_channel = 0u;
}

void usb_hid_poll(void)
{
    if (usb_hid_command_status == USB_HID_STATUS_PENDING) {
        usb_hid_execute_pending_save();
    }
    if (usb_hid_reboot_pending && time_reached(usb_hid_reboot_deadline)) {
        system_reset();
        return;
    }
    uint64_t now_ms = time_us_64() / 1000u;
    if ((int32_t)((uint32_t)now_ms - usb_hid_next_fault_sample_ms) >= 0) {
        for (uint8_t index = 0u; index < UART_PORT_COUNT; ++index) {
            (void)usb_hid_capture(index);
        }
        usb_hid_next_fault_sample_ms = (uint32_t)now_ms + USB_HID_FAULT_SAMPLE_INTERVAL_MS;
    }
    if (!tud_hid_ready()) {
        return;
    }
    for (uint8_t offset = 0u; offset < UART_PORT_COUNT; ++offset) {
        uint8_t index = (uint8_t)((usb_hid_next_channel + offset) % UART_PORT_COUNT);
        usb_hid_channel_t *channel = &usb_hid_channels[index];
        if (now_ms - channel->last_publication_ms < USB_HID_CHANNEL_INTERVAL_MS) {
            continue;
        }
        uint8_t payload[USB_HID_CHANNEL_SIZE];
        bool captured = usb_hid_capture(index);
        now_ms = time_us_64() / 1000u;
        captured = captured && usb_hid_build_channel(index, payload, true, now_ms);
        if (captured && tud_hid_report((uint8_t)(USB_HID_REPORT_ID_CHANNEL_BASE + index), payload, sizeof(payload))) {
            channel->sequence = (uint16_t)(channel->sequence + 1u);
            channel->last_publication_ms = now_ms;
            service_load_note(0u);
        }
        usb_hid_next_channel = (uint8_t)((index + 1u) % UART_PORT_COUNT);
        return;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t *buffer,
                               uint16_t reqlen)
{
    uint8_t payload[USB_HID_CONFIGURATION_SIZE];
    uint16_t length;
    if (instance != 0u || buffer == NULL) {
        return 0u;
    }
    if (type == HID_REPORT_TYPE_FEATURE && report_id == USB_HID_REPORT_ID_DEVICE_INFO) {
        memcpy(payload, usb_hid_identity, USB_HID_DEVICE_INFO_SIZE);
        length = USB_HID_DEVICE_INFO_SIZE;
    } else if (type == HID_REPORT_TYPE_FEATURE && report_id == USB_HID_REPORT_ID_CONFIGURATION) {
        memcpy(payload, usb_hid_configuration, USB_HID_CONFIGURATION_SIZE);
        length = USB_HID_CONFIGURATION_SIZE;
    } else if (type == HID_REPORT_TYPE_FEATURE && report_id == USB_HID_REPORT_ID_CONTROL) {
        const uint8_t result[] = {USB_HID_REPORT_VERSION, usb_hid_command,
                                  (uint8_t)usb_hid_command_status, usb_hid_error_reason};
        memcpy(payload, result, sizeof(result));
        length = sizeof(result);
    } else if (type == HID_REPORT_TYPE_FEATURE && report_id == USB_HID_REPORT_ID_TELEMETRY) {
        if (!usb_hid_build_telemetry(payload)) {
            return 0u;
        }
        length = USB_HID_TELEMETRY_SIZE;
    } else if ((type == HID_REPORT_TYPE_FEATURE || type == HID_REPORT_TYPE_INPUT) &&
               report_id >= USB_HID_REPORT_ID_CHANNEL_BASE &&
               report_id < USB_HID_REPORT_ID_CHANNEL_BASE + UART_PORT_COUNT) {
        uint8_t index = (uint8_t)(report_id - USB_HID_REPORT_ID_CHANNEL_BASE);
        if (!usb_hid_capture(index) ||
            !usb_hid_build_channel(index, payload, false, time_us_64() / 1000u)) {
            return 0u;
        }
        length = USB_HID_CHANNEL_SIZE;
    } else if (type == HID_REPORT_TYPE_FEATURE && report_id >= USB_HID_REPORT_ID_DIAGNOSTIC_BASE &&
               report_id < USB_HID_REPORT_ID_DIAGNOSTIC_BASE + UART_PORT_COUNT) {
        uint8_t index = (uint8_t)(report_id - USB_HID_REPORT_ID_DIAGNOSTIC_BASE);
        if (!usb_hid_capture(index)) {
            return 0u;
        }
        usb_hid_build_diagnostic(index, payload);
        length = USB_HID_DIAGNOSTIC_SIZE;
    } else {
        return 0u;
    }
    uint16_t count = reqlen < length ? reqlen : length;
    memcpy(buffer, payload, count);
    return count;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                           uint8_t const *buffer, uint16_t size)
{
    if (instance == 0u && type == HID_REPORT_TYPE_FEATURE && report_id == USB_HID_REPORT_ID_CONTROL) {
        (void)usb_hid_handle_command(buffer, size);
    }
}

/** @brief SDK HID handling for descriptor, idle, and protocol requests not intercepted below. */
bool __real_hidd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);

/**
 * @brief Handle report transfers before status ACK; delegate all other HID requests to TinyUSB.
 *
 * TinyUSB's standard SET callback runs at ACK and cannot reject a transfer. Its GET path also accepts an ID-only
 * response when the application returns zero. The linker adapter enforces protected writes and failed GETs without
 * modifying the SDK; the pinned TinyUSB transfer stages are exercised by native tests.
 */
bool __wrap_hidd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_CLASS ||
        (request->bRequest != HID_REQ_CONTROL_GET_REPORT && request->bRequest != HID_REQ_CONTROL_SET_REPORT)) {
        return __real_hidd_control_xfer_cb(rhport, stage, request);
    }
    uint8_t id = (uint8_t)request->wValue;
    hid_report_type_t type = (hid_report_type_t)(request->wValue >> 8);
    if (request->bRequest == HID_REQ_CONTROL_GET_REPORT) {
        if (request->bmRequestType_bit.direction != TUSB_DIR_IN) {
            return false;
        }
        if (stage == CONTROL_STAGE_SETUP) {
            uint16_t request_length = request->wLength < sizeof(usb_hid_control_buffer)
                ? request->wLength : (uint16_t)sizeof(usb_hid_control_buffer);
            uint16_t payload_length = tud_hid_get_report_cb(0u, id, type, usb_hid_control_buffer, request_length);
            if (payload_length == 0u) {
                return false;
            }
            uint16_t transfer_length = payload_length;
            if (id != 0u && request_length > payload_length) {
                memmove(usb_hid_control_buffer + 1u, usb_hid_control_buffer, payload_length);
                usb_hid_control_buffer[0] = id;
                ++transfer_length;
            }
            return tud_control_xfer(rhport, request, usb_hid_control_buffer, transfer_length);
        }
        return true;
    }
    if (request->bmRequestType_bit.direction != TUSB_DIR_OUT || type != HID_REPORT_TYPE_FEATURE ||
        id != USB_HID_REPORT_ID_CONTROL || usb_hid_command_status == USB_HID_STATUS_PENDING ||
        usb_hid_reboot_pending) {
        return false;
    }
    if (stage == CONTROL_STAGE_SETUP) {
        if (request->wLength == 0u || request->wLength > sizeof(usb_hid_control_buffer)) {
            usb_hid_set_result(0u, USB_HID_STATUS_INVALID, USB_HID_ERROR_MALFORMED);
            return false;
        }
        /* A short OUT packet must not synthesize valid zero flags/profile from unreceived bytes. */
        memset(usb_hid_control_buffer, 0xff, sizeof(usb_hid_control_buffer));
        return tud_control_xfer(rhport, request, usb_hid_control_buffer, request->wLength);
    }
    if (stage == CONTROL_STAGE_DATA) {
        const uint8_t *payload = usb_hid_control_buffer;
        uint16_t length = request->wLength;
        if (length > 1u && payload[0] == id) {
            ++payload;
            --length;
        }
        return usb_hid_handle_command(payload, length);
    }
    return true;
}
