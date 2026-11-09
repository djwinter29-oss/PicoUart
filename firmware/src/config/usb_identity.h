// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file usb_identity.h
 * @brief USB vendor/product identity for PicoUart firmware images.
 *
 * ## Policy
 *
 * The Raspberry Pi Foundation has allocated VID/PID `0x2E8A:0x116B` to
 * PicoUart; see https://github.com/raspberrypi/usb-pid. Keep host-side
 * discovery filters synchronized with these values.
 * Changing this identity is a breaking USB change; see docs/deployment/releasing.md
 * for the release and HIL checklist.
 */

#ifndef PICO_UART_USB_IDENTITY_H
#define PICO_UART_USB_IDENTITY_H

/** @brief Raspberry Pi Foundation USB vendor ID allocated for PicoUart. */
#define PICO_UART_USB_VID 0x2E8Au
/** @brief USB product ID allocated for PicoUart. */
#define PICO_UART_USB_PID 0x116Bu
/** @brief ASCII product name shared by USB enumeration and HID board identity. */
#define PICO_UART_USB_PRODUCT "PicoUart"

#endif
