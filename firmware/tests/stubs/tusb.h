/** @file tusb.h
 * @brief TinyUSB HID declarations for host-side payload tests.
 */
#ifndef TUSB_H
#define TUSB_H

#include <stdbool.h>
#include <stdint.h>

/** @brief TinyUSB HID report types. */
typedef enum {
    HID_REPORT_TYPE_INPUT = 1,   /**< Device input payload. */
    HID_REPORT_TYPE_OUTPUT = 2,  /**< Host output payload. */
    HID_REPORT_TYPE_FEATURE = 3, /**< Control-pipe feature payload. */
} hid_report_type_t;

/** @brief Return whether the fixture can transmit an input payload. */
bool tud_hid_ready(void);
/** @brief Accept a fixture input payload. */
bool tud_hid_report(uint8_t report_id, void const *report, uint16_t len);

#endif
