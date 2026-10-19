/**
 * @file usb_hid.c
 * @brief TinyUSB HID monitor helpers for PicoUart status reporting.
 */

#include "usb/usb_hid.h"

#include "config/capacity_config.h"
#include "config/usb_identity.h"
#include "driver/led.h"
#include "driver/service_load.h"
#include "driver/system.h"
#include "driver/temperature.h"
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
/** @brief HID feature report ID for cumulative per-port RX overflow counts. */
#define USB_HID_REPORT_ID_OVERFLOW_COUNTS 5u
/** @brief HID feature report ID for MCU identity and the current system clock. */
#define USB_HID_REPORT_ID_HARDWARE_INFO 6u
/** @brief Independent layout version of the hardware-info feature payload. */
#define USB_HID_HARDWARE_INFO_VERSION 1u
/** @brief Hardware-info MCU identifier for RP2040. */
#define USB_HID_MCU_RP2040 1u
/** @brief Hardware-info MCU identifier for RP2350. */
#define USB_HID_MCU_RP2350 2u
/** @brief HID feature report ID for product name and SDK unique board serial. */
#define USB_HID_REPORT_ID_IDENTITY 7u
/** @brief Independent identity feature payload layout version. */
#define USB_HID_IDENTITY_VERSION 1u
/** @brief HID feature report ID for uptime, active CPU time and reserved SRAM. */
#define USB_HID_REPORT_ID_RUNTIME 8u
/** @brief Independent runtime feature payload layout version. */
#define USB_HID_RUNTIME_VERSION 2u

/** @brief Independent LED feature report: version followed by availability/manual/output flags. */
#define USB_HID_REPORT_ID_LED 9u

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

/** @brief Board-status reserved0 bit 0: HID arm/reset commands are compiled in. */
#define USB_HID_BOARD_STATUS_FLAG_HID_RESET 1u

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
    uint8_t health;                     /**< UART status flags plus @ref USB_HID_CHANNEL_STATUS_* flags. */
    uint8_t ring_high_watermark_blocks; /**< Largest UART RX/TX ring occupancy in 16-byte blocks. */
    uint16_t controller_tx_bytes;       /**< Saturated controller TX byte delta since the preceding report. */
    uint16_t controller_rx_bytes;       /**< Saturated controller RX byte delta since the preceding report. */
    uint16_t cdc_tx_bytes;              /**< Saturated CDC-to-host byte delta since the preceding report. */
    uint16_t cdc_rx_bytes;              /**< Saturated host-to-CDC byte delta since the preceding report. */
} __attribute__((packed)) usb_hid_channel_status_t;

/**
 * @brief Compact HID monitor report published to the host.
 *
 * Layout is 63 bytes so TinyUSB can prepend Report ID 1 inside a 64-byte FS EP.
 */
typedef struct {
    uint8_t signature0;                                /**< Fixed report signature byte (`P`). */
    uint8_t version;                                   /**< Report layout version. */
    uint8_t sequence;                                  /**< Monotonic report sequence number. */
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
    uint8_t version;   /**< Report layout version. */
    uint8_t reserved0; /**< Board-status flags; bit 0 is @ref USB_HID_BOARD_STATUS_FLAG_HID_RESET. */
    int16_t temperature_centidegrees_celsius; /**< Internal temperature in hundredths of a degree Celsius. */
    uint8_t firmware_major;                   /**< Firmware semantic version major component. */
    uint8_t firmware_minor;                   /**< Firmware semantic version minor component. */
    uint8_t firmware_patch;                   /**< Firmware semantic version patch component. */
    uint8_t reserved1;                        /**< Reserved; always zero. */
} __attribute__((packed)) usb_hid_board_status_report_t;

_Static_assert(sizeof(usb_hid_board_status_report_t) == 8u,
               "HID board-status report must match the HID report descriptor");

/** @brief Feature report identifying the MCU and reporting its configured system clock. */
typedef struct {
    uint8_t version;         /**< Hardware-info layout version. */
    uint8_t mcu;             /**< MCU identifier: RP2040 or RP2350. */
    uint32_t system_clock_hz; /**< Current SDK-reported clk_sys frequency in Hz. */
} __attribute__((packed)) usb_hid_hardware_info_report_t;

_Static_assert(sizeof(usb_hid_hardware_info_report_t) == 6u,
               "HID hardware-info report must match the HID report descriptor");

/** @brief Product and unique board identity, independent of the MCU type in report 6. */
typedef struct {
    uint8_t version;  /**< Identity layout version. */
    char product[32]; /**< NUL-padded ASCII USB product name. */
    char serial[16];  /**< SDK board ID as ASCII hex, without a trailing NUL; flash-derived on RP2040. */
} __attribute__((packed)) usb_hid_identity_report_t;

_Static_assert(sizeof(usb_hid_identity_report_t) == 49u, "HID identity payload must be 49 bytes");
_Static_assert(offsetof(usb_hid_identity_report_t, serial) == 33u, "HID identity serial offset must be 33");
_Static_assert(sizeof(PICO_UART_USB_PRODUCT) <= 32u, "HID product name must fit with its trailing NUL");
_Static_assert(2u * PICO_UNIQUE_BOARD_ID_SIZE_BYTES == 16u, "HID serial must match the SDK board ID size");

/** @brief Little-endian runtime snapshot; packed integer fields target the RP2 little-endian ABI. */
typedef struct {
    uint8_t version;                  /**< Runtime layout version. */
    uint64_t uptime_ms;               /**< SDK monotonic microseconds since boot divided by 1000, without 32-bit wrap. */
    uint16_t service_load_basis_points[2]; /**< Measured per-core service-pass load; 0xffff until the first window. */
    uint32_t ram_reserved_bytes;      /**< Linked static SRAM extent, scratch sections and both reserved stacks. */
    uint32_t ram_total_bytes;         /**< Physical SRAM capacity, excluding XIP cache SRAM. */
} __attribute__((packed)) usb_hid_runtime_report_t;

_Static_assert(sizeof(usb_hid_runtime_report_t) == 21u, "HID runtime payload must be 21 bytes");
_Static_assert(offsetof(usb_hid_runtime_report_t, uptime_ms) == 1u, "HID uptime offset must be 1");
_Static_assert(offsetof(usb_hid_runtime_report_t, service_load_basis_points) == 9u, "HID service-load offset must be 9");
_Static_assert(offsetof(usb_hid_runtime_report_t, ram_reserved_bytes) == 13u, "HID reserved RAM offset must be 13");
_Static_assert(offsetof(usb_hid_runtime_report_t, ram_total_bytes) == 17u, "HID total RAM offset must be 17");

/** @brief SDK linker boundaries; addresses represent extents, not readable objects. */
extern char __end__, __scratch_x_start__, __scratch_x_end__, __scratch_y_start__, __scratch_y_end__;
/** @brief SDK linker boundaries for core 0 and core 1 stack reservations. */
extern char __StackBottom, __StackTop, __StackOneBottom, __StackOneTop;

/**
 * @brief Feature report containing cumulative UART-to-USB drop counts.
 */
typedef struct {
    uint8_t version;                             /**< HID report layout version. */
    uint32_t rx_overflow_count[UART_PORT_COUNT]; /**< Per-port dropped RX bytes, including pending recovery. */
} __attribute__((packed)) usb_hid_overflow_counts_report_t;

_Static_assert(sizeof(usb_hid_overflow_counts_report_t) == 25u,
               "HID overflow report must match the HID report descriptor");

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
#if PICO_UART_ALLOW_HID_RESET
    report->reserved0 = USB_HID_BOARD_STATUS_FLAG_HID_RESET;
#endif
    report->temperature_centidegrees_celsius = temperature_to_hid_centidegrees(temperature_read_celsius());
    report->firmware_major = (uint8_t)PICO_UART_VERSION_MAJOR;
    report->firmware_minor = (uint8_t)PICO_UART_VERSION_MINOR;
    report->firmware_patch = (uint8_t)PICO_UART_VERSION_PATCH;
}

/**
 * @brief Build identity from the same product definition and SDK serial API as USB enumeration.
 * @param report Destination feature payload.
 */
static void usb_hid_build_identity_report(usb_hid_identity_report_t *report)
{
    char serial[2u * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1u];

    memset(report, 0, sizeof(*report));
    report->version = USB_HID_IDENTITY_VERSION;
    memcpy(report->product, PICO_UART_USB_PRODUCT, sizeof(PICO_UART_USB_PRODUCT));
    pico_get_unique_board_id_string(serial, sizeof(serial));
    memcpy(report->serial, serial, sizeof(report->serial));
}

/**
 * @brief Snapshot measured service load and link-time SRAM reservations, not live heap/stack use.
 * @param report Destination feature payload.
 */
static void usb_hid_build_runtime_report(usb_hid_runtime_report_t *report)
{
    report->version = USB_HID_RUNTIME_VERSION;
    report->uptime_ms = time_us_64() / 1000u;
    report->service_load_basis_points[0] = service_load_read(0u);
    report->service_load_basis_points[1] = service_load_read(1u);
    report->ram_reserved_bytes = (uint32_t)(
        ((uintptr_t)&__end__ - SRAM_BASE) +
        ((uintptr_t)&__scratch_x_end__ - (uintptr_t)&__scratch_x_start__) +
        ((uintptr_t)&__scratch_y_end__ - (uintptr_t)&__scratch_y_start__) +
        ((uintptr_t)&__StackTop - (uintptr_t)&__StackBottom) +
        ((uintptr_t)&__StackOneTop - (uintptr_t)&__StackOneBottom));
    report->ram_total_bytes = SRAM_END - SRAM_BASE;
}

static void usb_hid_build_overflow_counts_report(usb_hid_overflow_counts_report_t *report,
                                                 const uart_driver_port_stats_t uart_stats[UART_PORT_COUNT])
{
    memset(report, 0, sizeof(*report));
    report->version = USB_HID_REPORT_VERSION;

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        report->rx_overflow_count[index] = uart_saturating_add_u32(uart_stats[index].rx_ring_overflow_count,
                                                                   uart_stats[index].rx_ring_pending_overflow_count);
    }
}

static void usb_hid_build_status_report(usb_hid_status_report_t *report,
                                        const uart_driver_port_stats_t uart_stats[UART_PORT_COUNT],
                                        const usb_cdc_port_stats_t cdc_stats[UART_PORT_COUNT])
{
    memset(report, 0, sizeof(*report));
    report->signature0 = USB_HID_SIGNATURE0;
    report->version = USB_HID_REPORT_VERSION;
    report->sequence = usb_hid_sequence;

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        uart_driver_port_info_t port_info;
        uint32_t ring_high_watermark;

        if (!uart_driver_port_info((uart_port_id_t)index, &port_info)) {
            continue;
        }

        report->channel[index].health = uart_driver_port_status((uart_port_id_t)index);
        if (cdc_stats[index].opened) {
            report->channel[index].health |= USB_HID_CHANNEL_STATUS_CDC_OPEN;
        }
        if (port_info.backend == UART_DRIVER_BACKEND_PIO) {
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
        report->channel[index].ring_high_watermark_blocks = usb_hid_clamp_u8((ring_high_watermark + 15u) / 16u);
        report->channel[index].controller_tx_bytes = usb_hid_clamp_u16(
            uart_stats[index].controller_tx_bytes - usb_hid_last_reported_uart_stats[index].controller_tx_bytes);
        report->channel[index].controller_rx_bytes = usb_hid_clamp_u16(
            uart_stats[index].controller_rx_bytes - usb_hid_last_reported_uart_stats[index].controller_rx_bytes);
        report->channel[index].cdc_tx_bytes =
            usb_hid_clamp_u16(cdc_stats[index].tx_bytes - usb_hid_last_reported_cdc_stats[index].tx_bytes);
        report->channel[index].cdc_rx_bytes =
            usb_hid_clamp_u16(cdc_stats[index].rx_bytes - usb_hid_last_reported_cdc_stats[index].rx_bytes);
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
    service_load_note(0u);

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
        /* Keep the signed deadline comparison bounded across very long outages. */
        usb_hid_next_report_ms = to_ms_since_boot(get_absolute_time()) + USB_HID_STATUS_INTERVAL_MS;
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

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    usb_hid_board_status_report_t board_status_report;
    usb_hid_hardware_info_report_t hardware_info_report;
    usb_hid_identity_report_t identity_report;
    usb_hid_runtime_report_t runtime_report;
    usb_hid_overflow_counts_report_t overflow_counts_report;
    usb_hid_status_report_t report;
    uart_driver_port_stats_t uart_stats[UART_PORT_COUNT];
    usb_cdc_port_stats_t cdc_stats[UART_PORT_COUNT];

    (void)instance;

    if ((report_type == HID_REPORT_TYPE_FEATURE) && (report_id == USB_HID_REPORT_ID_LED)) {
        bool manual_on, output_on;
        bool available = led_read_state(&manual_on, &output_on);
        const uint8_t led_report[2] = {1u, available ? (uint8_t)(1u | (manual_on ? 2u : 0u) |
                                                               (output_on ? 4u : 0u)) : 0u};
        if (reqlen > sizeof(led_report)) {
            reqlen = sizeof(led_report);
        }
        memcpy(buffer, led_report, reqlen);
        return reqlen;
    }

    if ((report_type == HID_REPORT_TYPE_FEATURE) && (report_id == USB_HID_REPORT_ID_IDENTITY)) {
        usb_hid_build_identity_report(&identity_report);
        if (reqlen > sizeof(identity_report)) {
            reqlen = sizeof(identity_report);
        }
        memcpy(buffer, &identity_report, reqlen);
        return reqlen;
    }

    if ((report_type == HID_REPORT_TYPE_FEATURE) && (report_id == USB_HID_REPORT_ID_RUNTIME)) {
        usb_hid_build_runtime_report(&runtime_report);
        if (reqlen > sizeof(runtime_report)) {
            reqlen = sizeof(runtime_report);
        }
        memcpy(buffer, &runtime_report, reqlen);
        return reqlen;
    }

        if ((report_type == HID_REPORT_TYPE_FEATURE) && (report_id == USB_HID_REPORT_ID_HARDWARE_INFO)) {
        hardware_info_report.version = USB_HID_HARDWARE_INFO_VERSION;
    #if PICO_RP2350
        hardware_info_report.mcu = USB_HID_MCU_RP2350;
    #else
        hardware_info_report.mcu = USB_HID_MCU_RP2040;
    #endif
        hardware_info_report.system_clock_hz = clock_get_hz(clk_sys);
        if (reqlen > sizeof(hardware_info_report)) {
            reqlen = sizeof(hardware_info_report);
        }
        memcpy(buffer, &hardware_info_report, reqlen);
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

    if ((report_type == HID_REPORT_TYPE_FEATURE) && (report_id == USB_HID_REPORT_ID_OVERFLOW_COUNTS)) {
        usb_hid_sample_stats(uart_stats, cdc_stats);
        usb_hid_build_overflow_counts_report(&overflow_counts_report, uart_stats);
        if (reqlen > sizeof(overflow_counts_report)) {
            reqlen = sizeof(overflow_counts_report);
        }

        memcpy(buffer, &overflow_counts_report, reqlen);
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

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    (void)instance;

    if ((report_id != USB_HID_REPORT_ID_COMMAND) || (report_type != HID_REPORT_TYPE_FEATURE) || (bufsize < 1u)) {
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

void usb_hid_reset_host_state(void)
{
    usb_hid_reset_armed_deadline = nil_time;
    usb_hid_next_report_ms = to_ms_since_boot(get_absolute_time());
}
