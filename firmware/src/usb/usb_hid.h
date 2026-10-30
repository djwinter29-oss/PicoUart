// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file usb_hid.h
 * @brief TinyUSB HID monitor helpers for PicoUart status reporting.
 */

#ifndef USB_HID_H
#define USB_HID_H

/** @brief Initialize static HID reports, action results, channel accounting, and fault history. */
void usb_hid_init(void);

/** @brief Execute deferred actions, harvest statistics, and publish one due Input snapshot when ready. */
void usb_hid_poll(void);

/** @brief Restart Input sequence/intervals on USB reset without clearing lifetime history or accepted actions. */
void usb_hid_reset_host_state(void);

#endif
