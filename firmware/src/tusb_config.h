#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#include "pico.h"
#include "tusb_option.h"

#ifndef CFG_TUSB_MCU
#if defined(PICO_RP2350)
#define CFG_TUSB_MCU OPT_MCU_RP2350
#else
#define CFG_TUSB_MCU OPT_MCU_RP2040
#endif
#endif

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_ENABLED 1

#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC 6
#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 64
#define CFG_TUD_CDC_EP_BUFSIZE 64

#define CFG_TUD_HID 1
#define CFG_TUD_HID_EP_BUFSIZE 32

#endif