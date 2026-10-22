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
