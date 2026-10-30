// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file led.h
 * @brief On/off board-identification LED control.
 */

#ifndef LED_H
#define LED_H

#include <stdbool.h>

/**
 * @brief Initialize the default board LED GPIO, if present, with identification off.
 */
void led_init(void);

/**
 * @brief Set the board-identification LED state; no-op when no LED is available.
 * @param on True to turn identification on, false to turn it off.
 */
void led_set(bool on);

/**
 * @brief Read the board-identification LED state.
 * @param on Receives the output state, or false when no LED is available.
 * @return True if the selected board has a default LED and @p on is non-null.
 */
bool led_read_state(bool *on);

#endif