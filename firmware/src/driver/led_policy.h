/**
 * @file led_policy.h
 * @brief Pure board-LED state merge logic shared by firmware and host tests.
 *
 * The board LED has two independent sources: a host-toggled manual state
 * (HID `toggle-led` command) and a transient USB-activity indicator driven by
 * CDC traffic. This header keeps the two states apart and defines how they
 * combine, so hardware code (@ref led.c) only needs to apply the merged
 * result to the GPIO. Free of Pico SDK includes so Unity host tests can lock
 * the merge behavior without stubbing hardware headers.
 */

#ifndef LED_POLICY_H
#define LED_POLICY_H

#include <stdbool.h>

/**
 * @brief Independently tracked LED sources merged into one GPIO output.
 */
typedef struct {
    bool manual_led_state; /**< Host-toggled state (HID `toggle-led`); persists until toggled again. */
    bool usb_activity_active; /**< True while the USB-activity window is open. */
} led_policy_state_t;

/**
 * @brief Reset both LED sources to off (used at init and USB enumeration reset).
 * @param state LED policy state to clear.
 */
static inline void led_policy_reset(led_policy_state_t *state)
{
    state->manual_led_state = false;
    state->usb_activity_active = false;
}

/**
 * @brief Flip the manual LED state, independent of USB activity.
 * @param state LED policy state to update.
 */
static inline void led_policy_toggle_manual(led_policy_state_t *state)
{
    state->manual_led_state = !state->manual_led_state;
}

/**
 * @brief Set the transient USB-activity state, independent of the manual state.
 * @param state LED policy state to update.
 * @param active True while a USB-activity window is open.
 */
static inline void led_policy_set_usb_activity(led_policy_state_t *state, bool active)
{
    state->usb_activity_active = active;
}

/**
 * @brief Merge the two LED sources into the physical GPIO output.
 * @param state LED policy state to read.
 * @return `true` when the LED should be lit (manual on OR USB activity active).
 */
static inline bool led_policy_output(const led_policy_state_t *state)
{
    return state->manual_led_state || state->usb_activity_active;
}

#endif
