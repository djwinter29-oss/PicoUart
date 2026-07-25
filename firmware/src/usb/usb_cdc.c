/**
 * @file usb_cdc.c
 * @brief TinyUSB CDC helpers for PicoUart USB-to-UART bridging.
 */

#include "usb/usb_cdc.h"

#include "config/config.h"
#include "uart/uart_driver.h"

#include "tusb.h"

/** @brief Number of USB CDC functions exposed by the firmware. */
#define USB_CDC_PORT_COUNT 6u
/** @brief Maximum bytes moved per interface and direction in one bridge pass. */
#define USB_CDC_BRIDGE_PASS_BUDGET 256u

/**
 * @brief Line-coding request deferred until the UART worker mailbox is available.
 */
typedef struct {
    bool pending; /**< True while the host request has not entered the worker mailbox. */
    uart_driver_line_coding_t line_coding; /**< Latest requested UART settings. */
} usb_cdc_pending_line_coding_t;

/** @brief Per-port host line-coding requests waiting for the worker mailbox. */
static usb_cdc_pending_line_coding_t usb_cdc_line_coding_pending[USB_CDC_PORT_COUNT];
/** @brief Per-port host-side CDC transport state. */
static usb_cdc_port_stats_t usb_cdc_stats[USB_CDC_PORT_COUNT];

static bool usb_cdc_parse_line_coding(cdc_line_coding_t const *usb_line_coding,
                                      uart_driver_line_coding_t *line_coding)
{
    if ((usb_line_coding == NULL) || (line_coding == NULL) || (usb_line_coding->bit_rate == 0u)) {
        return false;
    }

    line_coding->baud_rate = usb_line_coding->bit_rate;
    line_coding->data_bits = usb_line_coding->data_bits;

    if (usb_line_coding->stop_bits == 0u) {
        line_coding->stop_bits = 1u;
    } else if (usb_line_coding->stop_bits == 2u) {
        line_coding->stop_bits = 2u;
    } else {
        return false;
    }

    if (usb_line_coding->parity == 0u) {
        line_coding->parity = UART_DRIVER_PARITY_NONE;
    } else if (usb_line_coding->parity == 1u) {
        line_coding->parity = UART_DRIVER_PARITY_ODD;
    } else if (usb_line_coding->parity == 2u) {
        line_coding->parity = UART_DRIVER_PARITY_EVEN;
    } else {
        return false;
    }

    return true;
}

static bool usb_cdc_apply_line_coding(uint8_t itf,
                                      const uart_driver_line_coding_t *line_coding)
{
    if ((itf >= USB_CDC_PORT_COUNT) || (line_coding == NULL)) {
        return false;
    }

    if (!uart_driver_port_is_ready((uart_port_id_t)itf)) {
        return false;
    }

    return uart_driver_queue_line_coding((uart_port_id_t)itf, line_coding);
}

static void usb_cdc_apply_pending_line_coding(uint8_t itf)
{
    usb_cdc_pending_line_coding_t *pending = &usb_cdc_line_coding_pending[itf];

    if (!pending->pending) {
        return;
    }

    if (usb_cdc_apply_line_coding(itf, &pending->line_coding)) {
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

    if ((uart_driver_port_status((uart_port_id_t)itf) & UART_DRIVER_PORT_STATUS_CONTROL_PENDING) != 0u) {
        return;
    }

    available = tud_cdc_n_available(itf);
    if (available != 0u) {
        if (available > USB_CDC_BRIDGE_PASS_BUDGET) {
            available = USB_CDC_BRIDGE_PASS_BUDGET;
        }

        size_t drained = uart_driver_fill_tx((uart_port_id_t)itf,
                                             available,
                                             usb_cdc_usb_reader,
                                             &itf);

        if (drained != 0u) {
            usb_cdc_stats[itf].rx_bytes += (uint32_t)drained;
        }
    }
}

static uint32_t usb_cdc_usb_writer(void *context, const uint8_t *data, uint32_t length)
{
    uint8_t itf = *(const uint8_t *)context;
    return tud_cdc_n_write(itf, data, length);
}

static void usb_cdc_bridge_uart_to_usb(uint8_t itf)
{
    uint32_t writable = tud_cdc_n_write_available(itf);
    size_t written = 0u;

    if (writable != 0u) {
        if (writable > USB_CDC_BRIDGE_PASS_BUDGET) {
            writable = USB_CDC_BRIDGE_PASS_BUDGET;
        }

        written = uart_driver_drain_rx((uart_port_id_t)itf,
                                       writable,
                                       usb_cdc_usb_writer,
                                       &itf);
        if (written != 0u) {
            usb_cdc_stats[itf].tx_bytes += (uint32_t)written;
        }
    }

    if (written != 0u) {
        tud_cdc_n_write_flush(itf);
    }
}

void usb_cdc_init(void) {
    tusb_init();
}

void usb_cdc_poll(void) {
    tud_task();

    for (uint8_t itf = 0u; itf < USB_CDC_PORT_COUNT; ++itf) {
        usb_cdc_apply_pending_line_coding(itf);

        if (!uart_driver_port_is_ready((uart_port_id_t)itf)) {
            continue;
        }

        usb_cdc_bridge_usb_to_uart(itf);
        usb_cdc_bridge_uart_to_usb(itf);
    }
}

/**
 * @brief TinyUSB callback for CDC line-state changes.
 * @param itf TinyUSB CDC interface index.
 * @param dtr Host DTR state.
 * @param rts Host RTS state.
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

    if ((itf >= USB_CDC_PORT_COUNT) ||
        !usb_cdc_parse_line_coding(p_line_coding, &line_coding)) {
        return;
    }

    usb_cdc_line_coding_pending[itf].line_coding = line_coding;
    usb_cdc_line_coding_pending[itf].pending = true;
}

bool usb_cdc_port_stats(uint8_t itf, usb_cdc_port_stats_t *stats)
{
    if ((itf >= USB_CDC_PORT_COUNT) || (stats == NULL)) {
        return false;
    }

    *stats = usb_cdc_stats[itf];
    return true;
}