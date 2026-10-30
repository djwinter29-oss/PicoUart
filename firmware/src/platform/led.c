// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file led.c
 * @brief On/off board-identification LED control.
 */

#include "platform/led.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include <stddef.h>

/** @brief Identification output state, owned by core 0. */
static bool led_on;

/** @copydoc led_init */
void led_init(void)
{
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
    led_on = false;
}

/** @copydoc led_set */
void led_set(bool on)
{
#ifdef PICO_DEFAULT_LED_PIN
    led_on = on;
    gpio_put(PICO_DEFAULT_LED_PIN, on);
#else
    (void)on;
#endif
}

/** @copydoc led_read_state */
bool led_read_state(bool *on)
{
    if (on == NULL) {
        return false;
    }
    *on = led_on;
#ifdef PICO_DEFAULT_LED_PIN
    return true;
#else
    return false;
#endif
}
