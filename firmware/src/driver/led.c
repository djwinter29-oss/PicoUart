/**
 * @file led.c
 * @brief Default board LED helpers for PicoUart firmware.
 */

#include "driver/led.h"
#include "driver/led_policy.h"

#include "pico/stdlib.h"

/** @brief Merged manual/USB-activity LED state; see @ref led_policy.h. */
static led_policy_state_t led_state;

/** @brief Apply the current merged state to the physical LED GPIO, if present. */
static void led_apply(void)
{
    bool on = led_policy_output(&led_state);
#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, on ? 1 : 0);
#else
    (void)on;
#endif
}

/** @copydoc led_init */
void led_init(void)
{
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
    led_policy_reset(&led_state);
    led_apply();
}

/** @copydoc led_toggle */
void led_toggle(void)
{
    led_policy_toggle_manual(&led_state);
    led_apply();
}

/** @copydoc led_set_usb_activity */
void led_set_usb_activity(bool active)
{
    led_policy_set_usb_activity(&led_state, active);
    led_apply();
}
