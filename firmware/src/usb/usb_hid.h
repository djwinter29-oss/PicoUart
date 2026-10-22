/**
 * @file usb_hid.h
 * @brief TinyUSB HID monitor helpers for PicoUart status reporting.
 */

#ifndef USB_HID_H
#define USB_HID_H

/** @brief Initialize version-1 HID command, channel traffic, and fault-history state. */
void usb_hid_init(void);

/** @brief Harvest telemetry and publish one due per-channel Input snapshot when ready. */
void usb_hid_poll(void);

/** @brief Revoke reboot authorization and restart per-channel traffic baselines on USB reset/disconnect. */
void usb_hid_reset_host_state(void);

#endif
