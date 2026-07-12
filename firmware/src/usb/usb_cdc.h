/**
 * @file usb_cdc.h
 * @brief TinyUSB CDC helpers for the PicoUart USB echo firmware.
 */

#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Initialize the TinyUSB device stack and local CDC buffers. */
void usb_cdc_init(void);

/** @brief Run TinyUSB background work. */
void usb_cdc_poll(void);

#endif