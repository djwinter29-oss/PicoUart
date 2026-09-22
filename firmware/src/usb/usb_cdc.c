/**
 * @file usb_cdc.c
 * @brief TinyUSB CDC helpers for PicoUart USB-to-UART bridging.
 */

#include "usb/usb_cdc.h"

#include "config/capacity_config.h"
#include "uart/line_coding.h"
#include "uart/uart_driver.h"
#include "usb/cdc_soft_pending.h"
#include "usb/usb_hid.h"

#include "driver/led.h"
#include "driver/led_activity.h"
#include "pico/time.h"
#include "tusb.h"

/** @brief Number of USB CDC functions exposed by the firmware. */
#define USB_CDC_PORT_COUNT 6u
/**
 * @brief Maximum delay before a partial CDC IN buffer is flushed.
 */
#define USB_CDC_FLUSH_LATENCY_US 1000u
/**
 * @brief How long a CDC soft-pending line-coding request may wait for the mailbox.
 *
 * Matches the worker-side deferred-apply window so hosts see `control_error`
 * instead of an indefinite soft-pending stall when the mailbox never accepts.
 */
#define USB_CDC_SOFT_PENDING_TIMEOUT_MS 1000u

_Static_assert(USB_CDC_PORT_COUNT == UART_PORT_COUNT,
               "USB CDC port count must match the logical UART port table");

/**
 * @brief Line-coding request deferred until the UART worker mailbox is available.
 */
typedef struct {
    bool pending; /**< True while the host request has not entered the worker mailbox. */
    uint32_t control_generation; /**< Host request generation retained until mailbox submission. */
    absolute_time_t deadline; /**< Soft-pending expiry; ignored when @ref pending is false. */
    uart_driver_line_coding_t line_coding; /**< Latest requested UART settings. */
} usb_cdc_pending_line_coding_t;

/** @brief Per-port host line-coding requests waiting for the worker mailbox. */
static usb_cdc_pending_line_coding_t usb_cdc_line_coding_pending[USB_CDC_PORT_COUNT];
/** @brief Per-port host-side CDC transport state. */
static usb_cdc_port_stats_t usb_cdc_stats[USB_CDC_PORT_COUNT];
/** @brief Interface serviced first on the next bounded CDC bridge pass. */
static uint8_t usb_cdc_poll_start_itf;
/** @brief True when a partial CDC IN buffer is waiting for its latency deadline. */
static bool usb_cdc_tx_flush_pending[USB_CDC_PORT_COUNT];
/** @brief Flush deadlines for partial CDC IN buffers. */
static absolute_time_t usb_cdc_tx_flush_deadline[USB_CDC_PORT_COUNT];
/** @brief Open activity-LED deadline extended by any CDC transfer. */
static led_activity_window_t usb_cdc_activity_window;
/**
 * @brief How long the LED stays on after any CDC transfer (50 ms).
 *
 * Consecutive transfers extend the activity window so sustained traffic
 * appears continuously active.
 */
#define USB_CDC_ACTIVITY_LED_ON_US 50000u

static void usb_cdc_update_high_watermark(uint16_t *high_watermark, uint32_t occupancy)
{
    if (occupancy > *high_watermark) {
        *high_watermark = (occupancy > UINT16_MAX) ? UINT16_MAX : (uint16_t)occupancy;
    }
}

static bool usb_cdc_apply_line_coding(uint8_t itf,
                                      const uart_driver_line_coding_t *line_coding,
                                      uint32_t control_generation)
{
    if ((itf >= USB_CDC_PORT_COUNT) || (line_coding == NULL)) {
        return false;
    }

    if (!uart_driver_port_is_ready((uart_port_id_t)itf)) {
        return false;
    }

    return uart_driver_queue_line_coding((uart_port_id_t)itf, line_coding, control_generation);
}

static void usb_cdc_arm_soft_pending(uint8_t itf, const uart_driver_line_coding_t *line_coding)
{
    usb_cdc_pending_line_coding_t *pending = &usb_cdc_line_coding_pending[itf];
    bool was_pending = pending->pending;
    bool same_request = was_pending &&
                        (pending->line_coding.baud_rate == line_coding->baud_rate) &&
                        (pending->line_coding.data_bits == line_coding->data_bits) &&
                        (pending->line_coding.stop_bits == line_coding->stop_bits) &&
                        (pending->line_coding.parity == line_coding->parity);

    pending->line_coding = *line_coding;
    /* Identical retries cannot refresh forever; a distinct replacement gets its own window. */
    if (usb_cdc_soft_pending_should_set_deadline(was_pending, same_request)) {
        pending->deadline = make_timeout_time_ms(USB_CDC_SOFT_PENDING_TIMEOUT_MS);
    }
    pending->pending = true;
    /* HID-visible while waiting for the mailbox (before queue_line_coding). */
    pending->control_generation = uart_driver_mark_control_pending((uart_port_id_t)itf);
}

static void usb_cdc_reject_line_coding(uint8_t itf)
{
    /*
     * Surface CONTROL_ERROR for the bad host request. A prior valid soft-pending
     * request remains armed, but its completion is stale and cannot clear this
     * newer failure.
     */
    uart_driver_report_control_error((uart_port_id_t)itf);
}

static void usb_cdc_apply_pending_line_coding(uint8_t itf)
{
    usb_cdc_pending_line_coding_t *pending = &usb_cdc_line_coding_pending[itf];

    if (!pending->pending) {
        return;
    }

    /*
     * A reset (tud_mount_cb/tud_umount_cb) clears both `pending` and
     * `deadline` together, so this pairing should already hold. Short-circuit
     * on `is_nil_time` so `time_reached(nil_time)` (trivially true) is never
     * evaluated for a request that was cancelled rather than timed out; a
     * future call site that only clears one of the two fields cannot then
     * manufacture a spurious control_error. See usb_cdc_soft_pending_has_timed_out.
     */
    {
        bool deadline_is_nil = is_nil_time(pending->deadline);
        bool deadline_reached = !deadline_is_nil && time_reached(pending->deadline);
        if (usb_cdc_soft_pending_has_timed_out(deadline_is_nil, deadline_reached)) {
            pending->pending = false;
            uart_driver_report_soft_pending_error((uart_port_id_t)itf, pending->control_generation);
            return;
        }
    }

    /* Permanent rejects must not retry forever with soft-pending stuck true. */
    if (!uart_driver_line_coding_acceptable((uart_port_id_t)itf, &pending->line_coding)) {
        pending->pending = false;
        uart_driver_report_soft_pending_error((uart_port_id_t)itf, pending->control_generation);
        return;
    }

    if (usb_cdc_apply_line_coding(itf, &pending->line_coding, pending->control_generation)) {
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
    usb_cdc_update_high_watermark(&usb_cdc_stats[itf].rx_fifo_high_watermark, available);
    if (available != 0u) {
        if (available > PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET) {
            available = PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET;
        }

        size_t drained = uart_driver_fill_tx((uart_port_id_t)itf,
                                             available,
                                             usb_cdc_usb_reader,
                                             &itf);

        if (drained != 0u) {
            usb_cdc_stats[itf].rx_bytes += (uint32_t)drained;
            led_activity_window_note(&usb_cdc_activity_window,
                                     to_us_since_boot(get_absolute_time()),
                                     USB_CDC_ACTIVITY_LED_ON_US);
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
    if (usb_cdc_tx_flush_pending[itf] && time_reached(usb_cdc_tx_flush_deadline[itf])) {
        tud_cdc_n_write_flush(itf);
        usb_cdc_tx_flush_pending[itf] = false;
    }
}

/**
 * @brief Blink the board LED briefly when USB data transfers, then turn it off.
 *
 * Called once per poll cycle after the bridge pass. The LED lights for
 * @ref USB_CDC_ACTIVITY_LED_ON_US µs following any transfer on any CDC port,
 * providing a visible heartbeat without staying on continuously.
 */
static void usb_cdc_activity_led(void)
{
    bool active = led_activity_window_poll(&usb_cdc_activity_window,
                                           to_us_since_boot(get_absolute_time()));
    led_set_usb_activity(active);
}

static void usb_cdc_bridge_uart_to_usb(uint8_t itf)
{
    uint32_t writable;
    size_t written = 0u;

    usb_cdc_flush_if_due(itf);
    writable = tud_cdc_n_write_available(itf);
    usb_cdc_update_high_watermark(&usb_cdc_stats[itf].tx_fifo_high_watermark,
                                  PICO_UART_USB_CDC_TX_BUFFER_SIZE - writable);

    if (writable == 0u) {
        /* Still retire RX overruns so a stalled host cannot wrap the ring. */
        (void)uart_driver_recover_rx((uart_port_id_t)itf);
        return;
    }

    if (writable > PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET) {
        writable = PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET;
    }

    written = uart_driver_drain_rx((uart_port_id_t)itf,
                                   writable,
                                   usb_cdc_usb_writer,
                                   &itf);
    if (written != 0u) {
        usb_cdc_stats[itf].tx_bytes += (uint32_t)written;
        led_activity_window_note(&usb_cdc_activity_window,
                                 to_us_since_boot(get_absolute_time()),
                                 USB_CDC_ACTIVITY_LED_ON_US);
        if (!usb_cdc_tx_flush_pending[itf]) {
            usb_cdc_tx_flush_deadline[itf] = make_timeout_time_us(USB_CDC_FLUSH_LATENCY_US);
        }
        usb_cdc_tx_flush_pending[itf] = true;

        if ((written >= PICO_UART_USB_CDC_BRIDGE_PASS_BUDGET) ||
            (tud_cdc_n_write_available(itf) == 0u)) {
            tud_cdc_n_write_flush(itf);
            usb_cdc_tx_flush_pending[itf] = false;
        }
    }
}

void usb_cdc_init(void) {
    usb_cdc_poll_start_itf = 0u;
    for (uint8_t itf = 0u; itf < USB_CDC_PORT_COUNT; ++itf) {
        usb_cdc_tx_flush_pending[itf] = false;
        usb_cdc_tx_flush_deadline[itf] = nil_time;
    }
    led_activity_window_reset(&usb_cdc_activity_window);
    led_set_usb_activity(false);
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
        usb_cdc_stats[itf].opened = false;
        usb_cdc_line_coding_pending[itf].pending = false;
        usb_cdc_line_coding_pending[itf].deadline = nil_time;
        usb_cdc_tx_flush_pending[itf] = false;
        usb_cdc_tx_flush_deadline[itf] = nil_time;
        uart_driver_reset_soft_pending((uart_port_id_t)itf);
    }
    /* Stale activity from before a (re)enumeration should not keep the LED lit. */
    led_activity_window_reset(&usb_cdc_activity_window);
    led_set_usb_activity(false);
}

void tud_mount_cb(void)
{
    /* Reinitialize host-facing state after every USB enumeration. */
    usb_cdc_reset_host_state();
    usb_hid_reset_host_state();
}

void tud_umount_cb(void)
{
    usb_cdc_reset_host_state();
    usb_hid_reset_host_state();
}

void usb_cdc_poll(void) {
    uint8_t start_itf = usb_cdc_poll_start_itf;

    tud_task();

    for (uint8_t offset = 0u; offset < USB_CDC_PORT_COUNT; ++offset) {
        uint8_t itf = (uint8_t)((start_itf + offset) % USB_CDC_PORT_COUNT);

        usb_cdc_apply_pending_line_coding(itf);

        if (!uart_driver_port_is_ready((uart_port_id_t)itf)) {
            continue;
        }

        usb_cdc_bridge_usb_to_uart(itf);
        usb_cdc_bridge_uart_to_usb(itf);
    }

    usb_cdc_poll_start_itf = (uint8_t)((start_itf + 1u) % USB_CDC_PORT_COUNT);
    usb_cdc_activity_led();
}

/**
 * @brief TinyUSB callback for CDC line-state changes.
 * @param itf TinyUSB CDC interface index.
 * @param dtr Host DTR state (recorded for HID monitoring only; does not gate bridging).
 * @param rts Host RTS state (currently ignored; HW UART RTS/CTS is board-side).
 */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)rts;

    if (itf < USB_CDC_PORT_COUNT) {
        usb_cdc_stats[itf].opened = dtr;
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
        !uart_line_coding_from_usb(p_line_coding->bit_rate,
                                   p_line_coding->stop_bits,
                                   p_line_coding->parity,
                                   p_line_coding->data_bits,
                                   &line_coding)) {
        usb_cdc_reject_line_coding(itf);
        return;
    }

    /* Do not arm CDC soft-pending for permanent backend rejects (PIO 8N1/baud). */
    if (!uart_driver_line_coding_acceptable((uart_port_id_t)itf, &line_coding)) {
        usb_cdc_reject_line_coding(itf);
        return;
    }

    usb_cdc_arm_soft_pending(itf, &line_coding);
}

bool usb_cdc_port_stats(uint8_t itf, usb_cdc_port_stats_t *stats)
{
    if ((itf >= USB_CDC_PORT_COUNT) || (stats == NULL)) {
        return false;
    }

    *stats = usb_cdc_stats[itf];
    return true;
}
