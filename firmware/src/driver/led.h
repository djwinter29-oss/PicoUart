/**
 * @file led.h
 * @brief Default board LED helpers for PicoUart firmware.
 */

#ifndef LED_H
#define LED_H

#include <stdbool.h>

/**
 * @brief Initialize the default board LED GPIO when the selected board defines
 * one, and clear both the manual and USB-activity LED states.
 */
void led_init(void);

/**
 * @brief Toggle the manual board LED state (HID `toggle-led`), independent of
 * any USB-activity indication.
 */
void led_toggle(void);

/**
 * @brief Set the transient USB-activity LED state, independent of the manual state.
 *
 * The physical LED output is the OR of the manual state and this activity
 * state; see @ref led_policy_output.
 * @param active `true` while a USB-activity window is open.
 */
void led_set_usb_activity(bool active);

#endif