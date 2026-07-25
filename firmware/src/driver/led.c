/**
 * @file led.c
 * @brief Default board LED helpers for PicoUart firmware.
 */

#include "driver/led.h"

#include "pico/stdlib.h"

/** @copydoc led_init */
void led_init(void)
{
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
#endif
}

/** @copydoc led_toggle */
void led_toggle(void)
{
#ifdef PICO_DEFAULT_LED_PIN
    gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
#endif
}

/** @copydoc led_set */
void led_set(bool on)
{
#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, on ? 1 : 0);
#else
    (void)on;
#endif
}