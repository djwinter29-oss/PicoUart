// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file usb_cdc.c
 * @brief TinyUSB CDC helpers for PicoUart USB-to-UART bridging.
 */

#include "usb/usb_cdc.h"

#include "config/capacity_config.h"
#include "uart/bridge.h"
#include "uart/line_coding.h"
#include "uart/uart_driver.h"
#include "usb/cdc_soft_pending.h"
#include "usb/usb_hid.h"

#include "platform/service_load.h"
#include "pico/time.h"
#include "tusb.h"

/** @brief Number of USB CDC functions exposed by the firmware. */
#define USB_CDC_PORT_COUNT 6u
/**
 * @brief Maximum delay before a partial CDC IN buffer is flushed.
 */
#define USB_CDC_FLUSH_LATENCY_US 1000u
/**
 * @brief Bound mailbox staging after DTR opens and old-session stop completes.
 *
 * Initial settings can be staged before backend startup. Live changes wait for
 * backend readiness. Neither eligible mailbox submission can wait indefinitely.
 */
#define USB_CDC_SOFT_PENDING_TIMEOUT_MS 1000u

_Static_assert(USB_CDC_PORT_COUNT == UART_PORT_COUNT, "USB CDC port count must match the logical UART port table");

/**
 * @brief Line-coding request deferred until the UART worker mailbox is available.
 */
typedef struct {
    bool pending;                          /**< True while the host request has not entered the worker mailbox. */
    uint32_t control_generation;           /**< Host request generation retained until mailbox submission. */
    absolute_time_t deadline;              /**< Mailbox expiry, nil until staging is eligible. */
    uart_driver_line_coding_t line_coding; /**< Latest requested UART settings. */
} usb_cdc_pending_line_coding_t;

/** @brief Interface serviced first on the next bounded CDC bridge pass. */
static uint8_t usb_cdc_poll_start_itf;
/** @brief Core-0 scratch for serialized CDC RX draining; bridge callers own their own storage. */
static uint8_t usb_cdc_rx_snapshot[UART_BRIDGE_RX_SNAPSHOT_SIZE];
/** @brief True while the current TinyUSB configuration owns the CDC session state. */
static bool usb_cdc_session_active;

/** @brief USB-side admission for one DTR-open session, independent of live reconfiguration. */
typedef enum {
    USB_CDC_START_PENDING = 0, /**< Wait for old stop completion and stage initial settings. */
    USB_CDC_STARTING,         /**< Worker start requested; initial settings must complete before bridging. */
    USB_CDC_RUNNING,          /**< Initial configuration is complete; live RX draining is permitted. */
} usb_cdc_admission_t;

/** @brief Core-0-owned transport and control state for one CDC interface. */
typedef struct {
    usb_cdc_pending_line_coding_t line_coding; /**< Host request waiting for the worker mailbox. */
    usb_cdc_port_stats_t stats;               /**< DTR state and boot-lifetime transport counters. */
    absolute_time_t tx_flush_deadline;        /**< Deadline for flushing a partial CDC IN buffer. */
    usb_cdc_admission_t admission;            /**< Startup admission for the current DTR-open session. */
    bool reset_rx_flushed;                    /**< RX FIFO cleared for the current DTR-close reset. */
    bool tx_flush_pending;                    /**< Partial CDC IN buffer awaiting its flush deadline. */
} usb_cdc_port_t;

/** @brief Independent state for the six CDC interfaces; session reset preserves counters. */
static usb_cdc_port_t usb_cdc_ports[USB_CDC_PORT_COUNT];

static void usb_cdc_update_high_watermark(uint16_t *high_watermark, uint32_t occupancy)
{
    if (occupancy > *high_watermark) {
        *high_watermark = (occupancy > UINT16_MAX) ? UINT16_MAX : (uint16_t)occupancy;
    }
}

static void usb_cdc_arm_soft_pending(uint8_t itf, const uart_driver_line_coding_t *line_coding)
{
    usb_cdc_pending_line_coding_t *pending = &usb_cdc_ports[itf].line_coding;
    bool was_pending = pending->pending;
    bool same_request = was_pending && (pending->line_coding.baud_rate == line_coding->baud_rate) &&
                        (pending->line_coding.data_bits == line_coding->data_bits) &&
                        (pending->line_coding.stop_bits == line_coding->stop_bits) &&
                        (pending->line_coding.parity == line_coding->parity);

    pending->line_coding = *line_coding;
    /* Identical retries cannot refresh forever; a distinct replacement gets its own window. */
    if (usb_cdc_soft_pending_should_set_deadline(was_pending, same_request)) {
        pending->deadline = nil_time;
    }
    pending->pending = true;
    /* HID-visible while waiting for the mailbox (before queue_line_coding). */
    pending->control_generation = uart_driver_mark_control_pending((uart_port_id_t)itf);
}

static void usb_cdc_reject_line_coding(uint8_t itf, uart_driver_control_error_t reason)
{
    /*
     * Surface CONTROL_ERROR for the bad host request. A prior valid soft-pending
     * request remains armed, but its completion is stale and cannot clear this
     * newer failure.
     */
    uart_driver_report_control_error((uart_port_id_t)itf, reason);
}

/** @brief Stage pending settings after the caller establishes startup eligibility or live readiness. */
static void usb_cdc_apply_pending_line_coding(uint8_t itf)
{
    usb_cdc_pending_line_coding_t *pending = &usb_cdc_ports[itf].line_coding;

    if (!pending->pending) {
        return;
    }

    if (is_nil_time(pending->deadline)) {
        pending->deadline = make_timeout_time_ms(USB_CDC_SOFT_PENDING_TIMEOUT_MS);
    }
    if (time_reached(pending->deadline)) {
        service_load_note(0u);
        pending->pending = false;
        uart_driver_report_soft_pending_error((uart_port_id_t)itf, pending->control_generation,
                                               UART_CONTROL_ERROR_TIMEOUT);
        return;
    }

    /* Permanent rejects must not retry forever with soft-pending stuck true. */
    if (!uart_driver_line_coding_acceptable((uart_port_id_t)itf, &pending->line_coding)) {
        service_load_note(0u);
        pending->pending = false;
        uart_driver_report_soft_pending_error((uart_port_id_t)itf, pending->control_generation,
                                               UART_CONTROL_ERROR_OTHER);
        return;
    }

    if (uart_driver_queue_line_coding((uart_port_id_t)itf, &pending->line_coding, pending->control_generation)) {
        service_load_note(0u);
        pending->pending = false;
    }
}

static uint32_t usb_cdc_usb_reader(void *context, uint8_t *data, uint32_t length)
{
    uint8_t itf = *(const uint8_t *)context;
    return tud_cdc_n_read(itf, data, length);
}

static void usb_cdc_bridge_usb_to_uart(uint8_t itf)
{
    uint32_t available;

    available = tud_cdc_n_available(itf);
    usb_cdc_update_high_watermark(&usb_cdc_ports[itf].stats.rx_fifo_high_watermark, available);
    if (available != 0u) {
        if (available > PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET) {
            available = PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET;
        }

        size_t drained = uart_driver_fill_tx((uart_port_id_t)itf, available, usb_cdc_usb_reader, &itf);

        if (drained != 0u) {
            service_load_note(0u);
            usb_cdc_ports[itf].stats.rx_bytes += (uint32_t)drained;
        }
    }
}

static uint32_t usb_cdc_usb_writer(void *context, const uint8_t *data, uint32_t length)
{
    uint8_t itf = *(const uint8_t *)context;
    return tud_cdc_n_write(itf, data, length);
}

static void usb_cdc_flush_if_due(uint8_t itf)
{
    if (usb_cdc_ports[itf].tx_flush_pending && time_reached(usb_cdc_ports[itf].tx_flush_deadline)) {
        service_load_note(0u);
        tud_cdc_n_write_flush(itf);
        usb_cdc_ports[itf].tx_flush_pending = false;
    }
}

static void usb_cdc_bridge_uart_to_usb(uint8_t itf)
{
    uint32_t writable;
    size_t written = 0u;

    usb_cdc_flush_if_due(itf);
    writable = tud_cdc_n_write_available(itf);
    usb_cdc_update_high_watermark(&usb_cdc_ports[itf].stats.tx_fifo_high_watermark,
                                  PICO_UART_USB_CDC_TX_BUFFER_SIZE - writable);

    if (writable == 0u) {
        /* Still retire RX overruns so a stalled host cannot wrap the ring. */
        if (uart_driver_recover_rx((uart_port_id_t)itf) != 0u) {
            service_load_note(0u);
        }
        return;
    }

    if (writable > PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET) {
        writable = PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET;
    }

    written = uart_driver_drain_rx((uart_port_id_t)itf, writable, usb_cdc_usb_writer, &itf, usb_cdc_rx_snapshot,
                                   sizeof(usb_cdc_rx_snapshot));
    if (written != 0u) {
        service_load_note(0u);
        usb_cdc_ports[itf].stats.tx_bytes += (uint32_t)written;
        if (!usb_cdc_ports[itf].tx_flush_pending) {
            usb_cdc_ports[itf].tx_flush_deadline = make_timeout_time_us(USB_CDC_FLUSH_LATENCY_US);
        }
        usb_cdc_ports[itf].tx_flush_pending = true;

        if ((written >= PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET) || (tud_cdc_n_write_available(itf) == 0u)) {
            tud_cdc_n_write_flush(itf);
            usb_cdc_ports[itf].tx_flush_pending = false;
        }
    }
}

void usb_cdc_init(void)
{
    usb_cdc_poll_start_itf = 0u;
    for (uint8_t itf = 0u; itf < USB_CDC_PORT_COUNT; ++itf) {
        usb_cdc_ports[itf].tx_flush_pending = false;
        usb_cdc_ports[itf].tx_flush_deadline = nil_time;
    }
    tusb_init();

    /*
     * A debugger reset can leave the host USB controller holding the previous
     * configuration while the RP2040 USB peripheral starts fresh. Force a
     * visible disconnect before advertising the pull-up so the host performs
     * a complete enumeration instead of sending stale HID control requests.
     */
    tud_disconnect();
    sleep_ms(10u);
    tud_connect();
}

void usb_cdc_reset_host_state(void)
{
    for (uint8_t itf = 0u; itf < USB_CDC_PORT_COUNT; ++itf) {
        usb_cdc_port_t *port = &usb_cdc_ports[itf];
        if (port->stats.opened) {
            (void)uart_driver_request_channel_stop((uart_port_id_t)itf);
        }
        if (uart_driver_channel_stop_pending((uart_port_id_t)itf)) {
            uart_driver_channel_stop_usb_quiesced((uart_port_id_t)itf);
        }
        port->stats.opened = false;
        port->admission = USB_CDC_START_PENDING;
        port->line_coding.pending = false;
        port->line_coding.deadline = nil_time;
        port->tx_flush_pending = false;
        port->tx_flush_deadline = nil_time;
        uart_driver_reset_soft_pending((uart_port_id_t)itf);
    }
}

void tud_mount_cb(void)
{
    /* Reinitialize host-facing state after every USB enumeration. */
    usb_cdc_reset_host_state();
    usb_hid_reset_host_state();
    usb_cdc_session_active = true;
}

void tud_umount_cb(void)
{
    usb_cdc_reset_host_state();
    usb_hid_reset_host_state();
    usb_cdc_session_active = false;
}

void usb_cdc_poll(void)
{
    uint8_t start_itf = usb_cdc_poll_start_itf;

    if (tud_task_event_ready()) {
        service_load_note(0u);
    }
    tud_task();
    if (!tud_mounted()) {
        if (usb_cdc_session_active) {
            tud_umount_cb();
        }
        return;
    }

    for (uint8_t offset = 0u; offset < USB_CDC_PORT_COUNT; ++offset) {
        uint8_t itf = (uint8_t)((start_itf + offset) % USB_CDC_PORT_COUNT);
        usb_cdc_port_t *port = &usb_cdc_ports[itf];

        if (uart_driver_channel_stop_pending((uart_port_id_t)itf)) {
            if (!uart_driver_channel_stop_is_quiesced((uart_port_id_t)itf)) {
                if (!port->reset_rx_flushed) {
                    tud_cdc_n_read_flush(itf);
                    port->reset_rx_flushed = true;
                }
                if (tud_cdc_n_write_clear(itf)) {
                    uart_driver_channel_stop_usb_quiesced((uart_port_id_t)itf);
                }
            }
            continue;
        }

        if (!port->stats.opened) {
            if (tud_cdc_n_available(itf) != 0u) {
                tud_cdc_n_read_flush(itf);
            }
            continue;
        }

        if (port->admission == USB_CDC_START_PENDING) {
            usb_cdc_apply_pending_line_coding(itf);
            if (port->line_coding.pending) {
                continue;
            }
            if (!uart_driver_request_channel_start((uart_port_id_t)itf)) {
                uart_driver_report_control_error((uart_port_id_t)itf, UART_CONTROL_ERROR_BACKEND);
                continue;
            }
            port->admission = USB_CDC_STARTING;
        }

        if (!uart_driver_port_is_ready((uart_port_id_t)itf)) {
            continue;
        }
        usb_cdc_apply_pending_line_coding(itf);
        if (port->admission == USB_CDC_STARTING) {
            if (uart_driver_port_status((uart_port_id_t)itf) &
                (UART_DRIVER_PORT_STATUS_CONTROL_PENDING | UART_DRIVER_PORT_STATUS_CONTROL_ERROR |
                 UART_DRIVER_PORT_STATUS_INIT_FAILED)) {
                continue;
            }
            port->admission = USB_CDC_RUNNING;
        }

        usb_cdc_bridge_usb_to_uart(itf);
        usb_cdc_bridge_uart_to_usb(itf);
    }

    usb_cdc_poll_start_itf = (uint8_t)((start_itf + 1u) % USB_CDC_PORT_COUNT);
}

/**
 * @brief TinyUSB callback for CDC line-state changes.
 * @param itf TinyUSB CDC interface index.
 * @param dtr Host DTR state; opening starts the backend and closing stops it.
 * @param rts Host RTS state (currently ignored; HW UART RTS/CTS is board-side).
 */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    bool was_open;
    (void)rts;

    if (itf < USB_CDC_PORT_COUNT) {
        usb_cdc_port_t *port = &usb_cdc_ports[itf];
        was_open = port->stats.opened;
        if (usb_cdc_dtr_close_should_stop_channel(was_open, dtr)) {
            port->admission = USB_CDC_START_PENDING;
            port->line_coding.pending = false;
            port->line_coding.deadline = nil_time;
            port->tx_flush_pending = false;
            port->tx_flush_deadline = nil_time;
            uart_driver_reset_soft_pending((uart_port_id_t)itf);
            if (uart_driver_request_channel_stop((uart_port_id_t)itf)) {
                port->reset_rx_flushed = false;
            }
        } else if (!was_open && dtr) {
            port->admission = USB_CDC_START_PENDING;
        }
        port->stats.opened = dtr;
    }
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding)
{
    uart_driver_line_coding_t line_coding;

    if (itf >= USB_CDC_PORT_COUNT) {
        return;
    }

    /*
     * TinyUSB accepts SET_LINE_CODING at the USB layer before this callback.
     * Parse failures and later backend rejects are therefore invisible on the
     * CDC control pipe; surface them through HID CONTROL_ERROR instead.
     * PIO interface strings advertise "PIO 8N1" so hosts that ignore HID still
     * see the format limit in Device Manager / lsusb.
     */
    if ((p_line_coding == NULL) ||
        !uart_line_coding_from_usb(p_line_coding->bit_rate, p_line_coding->stop_bits, p_line_coding->parity,
                                   p_line_coding->data_bits, &line_coding)) {
        usb_cdc_reject_line_coding(itf, p_line_coding == NULL ? UART_CONTROL_ERROR_OTHER :
            (p_line_coding->bit_rate < UART_LINE_CODING_BAUD_MIN ||
             p_line_coding->bit_rate > UART_LINE_CODING_BAUD_MAX) ?
                UART_CONTROL_ERROR_BAUD : UART_CONTROL_ERROR_FRAMING);
        return;
    }

    /* Do not arm CDC soft-pending for permanent backend rejects (PIO 8N1/baud). */
    if (!uart_driver_line_coding_acceptable((uart_port_id_t)itf, &line_coding)) {
        uart_driver_port_info_t info;
        uart_driver_control_error_t reason = UART_CONTROL_ERROR_OTHER;
        if (uart_driver_port_info((uart_port_id_t)itf, &info)) {
            reason = info.backend == UART_DRIVER_BACKEND_PIO &&
                     (line_coding.data_bits != 8u || line_coding.stop_bits != 1u ||
                      line_coding.parity != UART_DRIVER_PARITY_NONE) ?
                         UART_CONTROL_ERROR_FRAMING : UART_CONTROL_ERROR_BAUD;
        }
        usb_cdc_reject_line_coding(itf, reason);
        return;
    }

    usb_cdc_arm_soft_pending(itf, &line_coding);
}

bool usb_cdc_port_stats(uint8_t itf, usb_cdc_port_stats_t *stats)
{
    if ((itf >= USB_CDC_PORT_COUNT) || (stats == NULL)) {
        return false;
    }

    *stats = usb_cdc_ports[itf].stats;
    return true;
}
