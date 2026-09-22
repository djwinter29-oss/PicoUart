/**
 * @file tusb_config.h
 * @brief TinyUSB device-stack configuration for PicoUart.
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#include "capacity_config.h"

#include "pico.h"
#include "tusb_option.h"

#ifndef CFG_TUSB_MCU
#if defined(PICO_RP2350)
/** @brief TinyUSB MCU family selected for RP2350 builds. */
#define CFG_TUSB_MCU OPT_MCU_RP2350
#else
/** @brief TinyUSB MCU family selected for RP2040 builds. */
#define CFG_TUSB_MCU OPT_MCU_RP2040
#endif
#endif

/** @brief Run TinyUSB as a full-speed device on root port 0. */
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
/** @brief Enable the TinyUSB device stack. */
#define CFG_TUD_ENABLED 1

/** @brief USB control endpoint maximum packet size. */
#define CFG_TUD_ENDPOINT0_SIZE PICO_UART_USB_CONTROL_ENDPOINT_BUFFER_SIZE

/** @brief Number of USB CDC functions exposed by the firmware. */
#define CFG_TUD_CDC 6
/** @brief Per-interface CDC OUT FIFO capacity. */
#define CFG_TUD_CDC_RX_BUFSIZE PICO_UART_USB_CDC_RX_BUFFER_SIZE
/** @brief Per-interface CDC IN FIFO capacity. */
#define CFG_TUD_CDC_TX_BUFSIZE PICO_UART_USB_CDC_TX_BUFFER_SIZE
/** @brief CDC endpoint maximum packet size. */
#define CFG_TUD_CDC_EP_BUFSIZE PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE

/** @brief Enable the vendor HID monitor interface. */
#define CFG_TUD_HID 1
/** @brief HID interrupt endpoint maximum packet size. */
#define CFG_TUD_HID_EP_BUFSIZE PICO_UART_USB_HID_ENDPOINT_BUFFER_SIZE

#endif