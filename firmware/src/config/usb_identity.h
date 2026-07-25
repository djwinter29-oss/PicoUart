/**
 * @file usb_identity.h
 * @brief USB vendor/product identity for PicoUart firmware images.
 *
 * ## Policy
 *
 * `0xCAFE:0x4010` is a **development / lab placeholder**. It is not an
 * allocated USB-IF or pid.codes identity. Do not ship production devices with
 * these IDs: collisions with other projects are likely, and Windows/macOS may
 * treat the device inconsistently across machines.
 *
 * Before any public or production release:
 * 1. Obtain an allocated VID/PID (for example via https://pid.codes or a
 *    commercial USB-IF vendor ID).
 * 2. Update @ref PICO_UART_USB_VID and @ref PICO_UART_USB_PID here.
 * 3. Keep @ref host/python/pico_uart_hid.py in sync (same numeric values).
 * 4. Call out the identity change in release notes as a breaking USB change.
 */

#ifndef PICO_UART_USB_IDENTITY_H
#define PICO_UART_USB_IDENTITY_H

/** @brief USB vendor ID used by PicoUart (development placeholder). */
#define PICO_UART_USB_VID 0xCafeu
/** @brief USB product ID used by PicoUart (development placeholder). */
#define PICO_UART_USB_PID 0x4010u

#endif
