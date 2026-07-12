/**
 * @file usb_cdc.c
 * @brief TinyUSB CDC helpers for the PicoUart USB echo firmware.
 */

#include "usb/usb_cdc.h"

#include "tusb.h"

/** @brief Maximum bytes read per TinyUSB CDC receive callback chunk. */
#define USB_CDC_PACKET_SIZE 64u

void usb_cdc_init(void) {
    tusb_init();
}

void usb_cdc_poll(void) {
    tud_task();
}

/**
 * @brief TinyUSB callback that echoes received bytes back on the same CDC port.
 * @param itf TinyUSB CDC interface index that received data.
 */
void tud_cdc_rx_cb(uint8_t itf) {
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
                /* ponytail: echo mode can drop the rest of a packet when the IN FIFO is full; this is acceptable for the current smoke-test firmware and can be replaced with per-port buffering when UART bridging is added. */
                break;
            }

            offset += written;
        }

        tud_cdc_n_write_flush(itf);
    }
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