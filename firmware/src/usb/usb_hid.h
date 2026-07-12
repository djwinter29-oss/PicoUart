/**
 * @file usb_hid.h
 * @brief TinyUSB HID monitor helpers for PicoUart status reporting.
 */

#ifndef USB_HID_H
#define USB_HID_H

/** @brief Initialize the HID monitor state. */
void usb_hid_init(void);

/** @brief Poll the HID monitor and publish the next status report when ready. */
void usb_hid_poll(void);

#endif