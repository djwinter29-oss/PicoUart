/**
 * @file led.h
 * @brief Default board LED helpers for PicoUart firmware.
 */

#ifndef LED_H
#define LED_H

#include <stdbool.h>

/** @brief Initialize the default board LED GPIO when the selected board defines one. */
void led_init(void);

/** @brief Toggle the default board LED state when one is available. */
void led_toggle(void);

/**
 * @brief Set the default board LED state when one is available.
 * @param on `true` requests LED on; `false` requests LED off.
 */
void led_set(bool on);

#endif