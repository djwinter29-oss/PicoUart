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

static void usb_cdc_echo_port(uint8_t itf)
{
    uint8_t buffer[USB_CDC_PACKET_SIZE];

    while (tud_cdc_n_available(itf) != 0u) {
        uint32_t count = tud_cdc_n_read(itf, buffer, sizeof(buffer));
        uint32_t offset = 0u;

        if (count == 0u) {
            break;
        }

        while (offset < count) {
            uint32_t written = tud_cdc_n_write(itf, buffer + offset, count - offset);

            if (written == 0u) {
                break;
            }

            offset += written;
        }

        tud_cdc_n_write_flush(itf);
    }
}

static void usb_cdc_bridge_usb_to_uart(uint8_t itf)
{
    uint8_t buffer[USB_CDC_PACKET_SIZE];

    while (tud_cdc_n_available(itf) != 0u) {
        size_t writable = uart_driver_write_available((uart_port_id_t)itf);
        uint32_t count;
        uint32_t offset = 0u;

        if (writable == 0u) {
            break;
        }

        count = (uint32_t)writable;
        if (count > sizeof(buffer)) {
            count = sizeof(buffer);
        }

        count = tud_cdc_n_read(itf, buffer, count);

        if (count == 0u) {
            break;
        }

        while (offset < count) {
            size_t written = uart_driver_write((uart_port_id_t)itf, buffer + offset, count - offset);

            if (written == 0u) {
                return;
            }

            offset += written;
        }
    }
}

static void usb_cdc_bridge_uart_to_usb(uint8_t itf)
{
    uint8_t buffer[USB_CDC_PACKET_SIZE];

    while (tud_cdc_n_write_available(itf) != 0u) {
        uint32_t available = tud_cdc_n_write_available(itf);
        uint32_t chunk = available;
        size_t count;

        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }

        count = uart_driver_read((uart_port_id_t)itf, buffer, chunk);
        if (count == 0u) {
            break;
        }

        if (tud_cdc_n_write(itf, buffer, (uint32_t)count) != count) {
            break;
        }

        tud_cdc_n_write_flush(itf);
    }
}

void usb_cdc_init(void) {
    tusb_init();
}

void usb_cdc_poll(void) {
    tud_task();

    for (uint8_t itf = 0u; itf < USB_CDC_PORT_COUNT; ++itf) {
        if (uart_driver_port_is_ready((uart_port_id_t)itf)) {
            usb_cdc_bridge_usb_to_uart(itf);
            usb_cdc_bridge_uart_to_usb(itf);
            continue;
        }

        usb_cdc_echo_port(itf);
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
    if (p_line_coding == NULL) {
        return;
    }

    (void)uart_driver_set_baud_rate((uart_port_id_t)itf, p_line_coding->bit_rate);
}