/**
 * @file led_activity.h
 * @brief Pure USB-activity LED timeout window shared by firmware and host tests.
 *
 * Tracks a single "activity is on until this microsecond deadline" window
 * without depending on Pico SDK time types, so Unity host tests can lock the
 * extend/expire behavior. @ref usb_cdc.c supplies real microsecond timestamps
 * (`to_us_since_boot`) and feeds the result into @ref led_policy_set_usb_activity.
 */

#ifndef LED_ACTIVITY_H
#define LED_ACTIVITY_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Open-ended activity deadline tracked in absolute microseconds.
 */
typedef struct {
    bool has_deadline; /**< True while an activity deadline is pending. */
    uint64_t deadline_us; /**< Absolute microsecond deadline; ignored when @ref has_deadline is false. */
} led_activity_window_t;

/** @brief Clear the activity window (used at init and USB enumeration reset). */
static inline void led_activity_window_reset(led_activity_window_t *window)
{
    window->has_deadline = false;
    window->deadline_us = 0u;
}

/**
 * @brief Record activity and (re)arm the window `window_us` microseconds out.
 * @param window Activity window to update.
 * @param now_us Current absolute time in microseconds.
 * @param window_us How long the window stays open after this call.
 *
 * Calling this again before expiry extends the deadline from `now_us`, so
 * back-to-back activity keeps the LED lit continuously instead of blinking.
 */
static inline void led_activity_window_note(led_activity_window_t *window,
                                            uint64_t now_us,
                                            uint64_t window_us)
{
    window->has_deadline = true;
    window->deadline_us = now_us + window_us;
}

/**
 * @brief Poll whether the activity window is still open, expiring it if not.
 * @param window Activity window to poll (mutated when it expires).
 * @param now_us Current absolute time in microseconds.
 * @return `true` while the window remains open at @p now_us.
 */
static inline bool led_activity_window_poll(led_activity_window_t *window, uint64_t now_us)
{
    if (!window->has_deadline) {
        return false;
    }
    if (now_us >= window->deadline_us) {
        window->has_deadline = false;
        return false;
    }
    return true;
}

#endif
