// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
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

enum {
    TUSB_REQ_TYPE_CLASS = 1, TUSB_DIR_OUT = 0, TUSB_DIR_IN = 1,
    HID_REQ_CONTROL_GET_REPORT = 1, HID_REQ_CONTROL_SET_REPORT = 9,
    CONTROL_STAGE_SETUP = 1, CONTROL_STAGE_DATA = 2, CONTROL_STAGE_ACK = 3,
};

typedef struct {
    struct {
        uint8_t recipient : 5;
        uint8_t type : 2;
        uint8_t direction : 1;
    } bmRequestType_bit;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} tusb_control_request_t;

bool tud_control_xfer(uint8_t rhport, tusb_control_request_t const *request, void *buffer, uint16_t length);

/** @brief Return whether the fixture can transmit an input payload. */
bool tud_hid_ready(void);
/** @brief Accept a fixture input payload. */
bool tud_hid_report(uint8_t report_id, void const *report, uint16_t len);

/** @brief CDC line coding passed to the production callbacks. */
typedef struct {
    uint32_t bit_rate; /**< Requested baud rate. */
    uint8_t stop_bits; /**< USB stop-bit encoding. */
    uint8_t parity; /**< USB parity encoding. */
    uint8_t data_bits; /**< Requested data width. */
} cdc_line_coding_t;

void tusb_init(void);
bool tud_disconnect(void);
bool tud_connect(void);
bool tud_task_event_ready(void);
void tud_task(void);
bool tud_mounted(void);
uint32_t tud_cdc_n_available(uint8_t itf);
uint32_t tud_cdc_n_read(uint8_t itf, void *buffer, uint32_t length);
void tud_cdc_n_read_flush(uint8_t itf);
uint32_t tud_cdc_n_write(uint8_t itf, const void *buffer, uint32_t length);
uint32_t tud_cdc_n_write_available(uint8_t itf);
uint32_t tud_cdc_n_write_flush(uint8_t itf);
bool tud_cdc_n_write_clear(uint8_t itf);
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts);
void tud_cdc_line_coding_cb(uint8_t itf, const cdc_line_coding_t *coding);
void tud_mount_cb(void);
void tud_umount_cb(void);

#endif
