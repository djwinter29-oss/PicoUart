/**
 * @file usb_cdc.h
 * @brief TinyUSB CDC helpers for the PicoUart USB-to-UART bridge firmware.
 */

#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Host-side transport state for one CDC interface.
 */
typedef struct {
	bool opened; /**< True after the host asserts DTR for the interface. */
	uint32_t tx_bytes; /**< Bytes completed from the controller to the USB host. */
	uint32_t rx_bytes; /**< Bytes read from the USB host for the controller. */
} usb_cdc_port_stats_t;

/** @brief Initialize the TinyUSB device stack for the CDC bridge. */
void usb_cdc_init(void);

/** @brief Run TinyUSB background work. */
void usb_cdc_poll(void);

/**
 * @brief Snapshot host-side transport state for one CDC interface.
 * @param itf TinyUSB CDC interface index.
 * @param stats Output storage for the snapshot.
 * @return `true` when @p stats was written, otherwise `false`.
 */
bool usb_cdc_port_stats(uint8_t itf, usb_cdc_port_stats_t *stats);

#endif