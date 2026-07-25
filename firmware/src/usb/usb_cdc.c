/**
 * @file usb_cdc.c
 * @brief TinyUSB CDC helpers for PicoUart USB-to-UART bridging.
 */

#include "usb/usb_cdc.h"

#include "driver/uart_driver.h"

#include "tusb.h"

/** @brief Maximum bytes read per TinyUSB CDC receive callback chunk. */
#define USB_CDC_PACKET_SIZE 64u
/** @brief Number of USB CDC functions exposed by the firmware. */
#define USB_CDC_PORT_COUNT 6u

/**
 * @brief One per-port staging buffer used to survive partial bridge writes.
 */
typedef struct {
    uint8_t data[USB_CDC_PACKET_SIZE]; /**< Buffered bytes waiting to be forwarded. */
    uint32_t count; /**< Total buffered byte count. */
    uint32_t offset; /**< First unsent byte inside @ref data. */
} usb_cdc_pending_transfer_t;

/**
 * @brief Line-coding request deferred until the UART worker mailbox is available.
 */
typedef struct {
    bool pending; /**< True while the host request has not entered the worker mailbox. */
    uart_driver_line_coding_t line_coding; /**< Latest requested UART settings. */
} usb_cdc_pending_line_coding_t;

/** @brief Per-port USB-to-UART staging state. */
static usb_cdc_pending_transfer_t usb_cdc_usb_to_uart_pending[USB_CDC_PORT_COUNT];
/** @brief Per-port UART-to-USB staging state. */
static usb_cdc_pending_transfer_t usb_cdc_uart_to_usb_pending[USB_CDC_PORT_COUNT];
/** @brief Per-port host line-coding requests waiting for the worker mailbox. */
static usb_cdc_pending_line_coding_t usb_cdc_line_coding_pending[USB_CDC_PORT_COUNT];

static uint32_t usb_cdc_pending_length(const usb_cdc_pending_transfer_t *pending)
{
    if ((pending == NULL) || (pending->offset >= pending->count)) {
        return 0u;
    }

    return pending->count - pending->offset;
}

static void usb_cdc_pending_reset(usb_cdc_pending_transfer_t *pending)
{
    if (pending == NULL) {
        return;
    }

    pending->count = 0u;
    pending->offset = 0u;
}

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

static void usb_cdc_flush_usb_to_uart(uint8_t itf)
{
    usb_cdc_pending_transfer_t *pending = &usb_cdc_usb_to_uart_pending[itf];

    while (usb_cdc_pending_length(pending) != 0u) {
        size_t written = uart_driver_write((uart_port_id_t)itf,
                                           pending->data + pending->offset,
                                           usb_cdc_pending_length(pending));

        if (written == 0u) {
            break;
        }

        pending->offset += (uint32_t)written;
    }

    if (usb_cdc_pending_length(pending) == 0u) {
        usb_cdc_pending_reset(pending);
    }
}

static void usb_cdc_flush_uart_to_usb(uint8_t itf)
{
    usb_cdc_pending_transfer_t *pending = &usb_cdc_uart_to_usb_pending[itf];
    bool wrote_any = false;

    while (usb_cdc_pending_length(pending) != 0u) {
        uint32_t written = tud_cdc_n_write(itf,
                                           pending->data + pending->offset,
                                           usb_cdc_pending_length(pending));

        if (written == 0u) {
            break;
        }

        pending->offset += written;
        wrote_any = true;
    }

    if (wrote_any) {
        tud_cdc_n_write_flush(itf);
    }

    if (usb_cdc_pending_length(pending) == 0u) {
        usb_cdc_pending_reset(pending);
    }
}

static void usb_cdc_bridge_usb_to_uart(uint8_t itf)
{
    usb_cdc_pending_transfer_t *pending = &usb_cdc_usb_to_uart_pending[itf];

    if ((uart_driver_port_status((uart_port_id_t)itf) & UART_DRIVER_PORT_STATUS_CONTROL_PENDING) != 0u) {
        return;
    }

    usb_cdc_flush_usb_to_uart(itf);
    if (usb_cdc_pending_length(pending) != 0u) {
        return;
    }

    while (tud_cdc_n_available(itf) != 0u) {
        uint32_t count = tud_cdc_n_read(itf, pending->data, sizeof(pending->data));

        if (count == 0u) {
            break;
        }

        pending->count = count;
        pending->offset = 0u;
        usb_cdc_flush_usb_to_uart(itf);
        if (usb_cdc_pending_length(pending) != 0u) {
            return;
        }
    }
}

static void usb_cdc_bridge_uart_to_usb(uint8_t itf)
{
    usb_cdc_pending_transfer_t *pending = &usb_cdc_uart_to_usb_pending[itf];

    usb_cdc_flush_uart_to_usb(itf);
    if (usb_cdc_pending_length(pending) != 0u) {
        return;
    }

    while (tud_cdc_n_write_available(itf) != 0u) {
        uint32_t available = tud_cdc_n_write_available(itf);
        uint32_t chunk = available;
        size_t count;

        if (chunk > sizeof(pending->data)) {
            chunk = sizeof(pending->data);
        }

        count = uart_driver_read((uart_port_id_t)itf, pending->data, chunk);
        if (count == 0u) {
            break;
        }

        pending->count = (uint32_t)count;
        pending->offset = 0u;
        usb_cdc_flush_uart_to_usb(itf);
        if (usb_cdc_pending_length(pending) != 0u) {
            return;
        }
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
 * @brief TinyUSB callback invoked when CDC receive data arrives.
 * @param itf TinyUSB CDC interface index that received data.
 */
void tud_cdc_rx_cb(uint8_t itf) {
    (void)itf;
}

/**
 * @brief TinyUSB callback for CDC line-state changes.
 * @param itf TinyUSB CDC interface index.
 * @param dtr Host DTR state.
 * @param rts Host RTS state.
 */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    (void)dtr;
    (void)rts;
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