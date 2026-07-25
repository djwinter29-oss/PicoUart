/**
 * @file usb_descriptors.c
 * @brief TinyUSB descriptor tables and callbacks for the PicoUart USB CDC device.
 */

#include "pico/unique_id.h"
#include "tusb.h"
#include <string.h>

/** @brief Total number of CDC functions exposed by the firmware. */
#define USB_CDC_COUNT 6u
/** @brief Number of HID monitor interfaces exposed by the firmware. */
#define USB_HID_COUNT 1u
/** @brief USB vendor ID used by the PicoUart firmware image. */
#define USB_VID 0xCafe
/** @brief USB product ID used by the PicoUart firmware image. */
#define USB_PID 0x4010
/** @brief USB device version advertised in the device descriptor. */
#define USB_BCD  0x0210

/** @brief Manufacturer string exposed in the USB string table. */
#define USB_STR_MANUFACTURER "PicoUart"
/** @brief Product string exposed in the USB string table. */
#define USB_STR_PRODUCT "PicoUart USB CDC + HID"

/** @brief USB interface numbering used inside the configuration descriptor. */
typedef enum {
    ITF_NUM_CDC0 = 0, /**< CDC0 control interface number. */
    ITF_NUM_CDC0_DATA, /**< CDC0 data interface number. */
    ITF_NUM_CDC1, /**< CDC1 control interface number. */
    ITF_NUM_CDC1_DATA, /**< CDC1 data interface number. */
    ITF_NUM_CDC2, /**< CDC2 control interface number. */
    ITF_NUM_CDC2_DATA, /**< CDC2 data interface number. */
    ITF_NUM_CDC3, /**< CDC3 control interface number. */
    ITF_NUM_CDC3_DATA, /**< CDC3 data interface number. */
    ITF_NUM_CDC4, /**< CDC4 control interface number. */
    ITF_NUM_CDC4_DATA, /**< CDC4 data interface number. */
    ITF_NUM_CDC5, /**< CDC5 control interface number. */
    ITF_NUM_CDC5_DATA, /**< CDC5 data interface number. */
    ITF_NUM_HID, /**< HID monitor interface number. */
    ITF_NUM_TOTAL /**< Total number of interfaces in the configuration. */
} usb_interface_number_t;

/** @brief USB string descriptor indices used by the device and configuration descriptors. */
typedef enum {
    STRID_LANGID = 0, /**< Language ID string descriptor index. */
    STRID_MANUFACTURER, /**< Manufacturer string descriptor index. */
    STRID_PRODUCT, /**< Product string descriptor index. */
    STRID_SERIAL, /**< Serial number string descriptor index. */
    STRID_CDC0, /**< CDC0 interface string descriptor index. */
    STRID_CDC1, /**< CDC1 interface string descriptor index. */
    STRID_CDC2, /**< CDC2 interface string descriptor index. */
    STRID_CDC3, /**< CDC3 interface string descriptor index. */
    STRID_CDC4, /**< CDC4 interface string descriptor index. */
    STRID_CDC5, /**< CDC5 interface string descriptor index. */
    STRID_HID, /**< HID monitor interface string descriptor index. */
} usb_string_id_t;

/** @brief Vendor HID report descriptor used for status monitoring and board controls. */
static uint8_t const hid_report_descriptor[] = {
    0x06, 0x00, 0xFF,
    0x09, 0x01,
    0xA1, 0x01,
    0x85, 0x01,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x40,
    0x09, 0x01,
    0x81, 0x02,
    0x85, 0x03,
    0x95, 0x04,
    0x09, 0x03,
    0xB1, 0x02,
    0x85, 0x04,
    0x95, 0x01,
    0x09, 0x04,
    0xB1, 0x02,
    0xC0,
};

/** @brief USB device descriptor returned to the host during enumeration. */
static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0101,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

/**
 * @brief TinyUSB callback returning the device descriptor.
 * @return Pointer to the static device descriptor.
 */
uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/** @brief Endpoint number for CDC notification IN traffic. */
#define EPNUM_CDC0_NOTIF 0x81
#define EPNUM_CDC1_NOTIF 0x83
#define EPNUM_CDC2_NOTIF 0x85
#define EPNUM_CDC3_NOTIF 0x87
#define EPNUM_CDC4_NOTIF 0x89
#define EPNUM_CDC5_NOTIF 0x8B
/** @brief Endpoint numbers for CDC data OUT traffic. */
#define EPNUM_CDC0_OUT   0x02
#define EPNUM_CDC1_OUT   0x04
#define EPNUM_CDC2_OUT   0x06
#define EPNUM_CDC3_OUT   0x08
#define EPNUM_CDC4_OUT   0x0A
#define EPNUM_CDC5_OUT   0x0C
/** @brief Endpoint numbers for CDC data IN traffic. */
#define EPNUM_CDC0_IN    0x82
#define EPNUM_CDC1_IN    0x84
#define EPNUM_CDC2_IN    0x86
#define EPNUM_CDC3_IN    0x88
#define EPNUM_CDC4_IN    0x8A
#define EPNUM_CDC5_IN    0x8C
#define EPNUM_HID_IN     0x8E
/** @brief Total bytes in the USB configuration descriptor. */
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + (USB_CDC_COUNT * TUD_CDC_DESC_LEN) + (USB_HID_COUNT * TUD_HID_DESC_LEN))

/** @brief Configuration descriptor containing 6 USB CDC functions. */
static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC0, STRID_CDC0, EPNUM_CDC0_NOTIF, PICO_UART_USB_CDC_NOTIFICATION_ENDPOINT_BUFFER_SIZE, EPNUM_CDC0_OUT, EPNUM_CDC0_IN, PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC1, STRID_CDC1, EPNUM_CDC1_NOTIF, PICO_UART_USB_CDC_NOTIFICATION_ENDPOINT_BUFFER_SIZE, EPNUM_CDC1_OUT, EPNUM_CDC1_IN, PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC2, STRID_CDC2, EPNUM_CDC2_NOTIF, PICO_UART_USB_CDC_NOTIFICATION_ENDPOINT_BUFFER_SIZE, EPNUM_CDC2_OUT, EPNUM_CDC2_IN, PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC3, STRID_CDC3, EPNUM_CDC3_NOTIF, PICO_UART_USB_CDC_NOTIFICATION_ENDPOINT_BUFFER_SIZE, EPNUM_CDC3_OUT, EPNUM_CDC3_IN, PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC4, STRID_CDC4, EPNUM_CDC4_NOTIF, PICO_UART_USB_CDC_NOTIFICATION_ENDPOINT_BUFFER_SIZE, EPNUM_CDC4_OUT, EPNUM_CDC4_IN, PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC5, STRID_CDC5, EPNUM_CDC5_NOTIF, PICO_UART_USB_CDC_NOTIFICATION_ENDPOINT_BUFFER_SIZE, EPNUM_CDC5_OUT, EPNUM_CDC5_IN, PICO_UART_USB_CDC_ENDPOINT_BUFFER_SIZE),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, STRID_HID, HID_ITF_PROTOCOL_NONE, sizeof(hid_report_descriptor), EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 5),
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return hid_report_descriptor;
}

/**
 * @brief TinyUSB callback returning the configuration descriptor.
 * @param index Configuration index requested by the host.
 * @return Pointer to the static configuration descriptor.
 */
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/** @brief USB string table backing the TinyUSB string-descriptor callback. */
static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    USB_STR_MANUFACTURER,
    USB_STR_PRODUCT,
    NULL,
    "CDC0",
    "CDC1",
    "CDC2",
    "CDC3",
    "CDC4",
    "CDC5",
    "Status Monitor",
};

/** @brief Scratch UTF-16 string descriptor buffer returned by TinyUSB string callbacks. */
static uint16_t desc_str[32 + 1];

/**
 * @brief TinyUSB callback returning one UTF-16 USB string descriptor.
 * @param index String descriptor index requested by the host.
 * @param langid Language ID requested by the host.
 * @return Pointer to the UTF-16 descriptor buffer, or `NULL` for an invalid index.
 */
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;

    switch (index) {
    case STRID_LANGID:
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
        break;
    case STRID_SERIAL:
        {
            char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

            pico_get_unique_board_id_string(serial, sizeof(serial));
            chr_count = strlen(serial);
            for (size_t i = 0; i < chr_count; i++) {
                desc_str[1 + i] = (uint16_t)serial[i];
            }
        }
        break;
    default:
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }
        {
            const char *str = string_desc_arr[index];
            if (str == NULL) {
                return NULL;
            }
            chr_count = strlen(str);
            for (size_t i = 0; i < chr_count; i++) {
                desc_str[1 + i] = (uint16_t)str[i];
            }
        }
        break;
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}