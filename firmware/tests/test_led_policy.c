/**
 * @file test_led_policy.c
 * @brief Unity tests for the manual/USB-activity LED merge and timeout window.
 *
 * These headers are free of Pico SDK includes (see @ref led_policy.h and
 * @ref led_activity.h), so this suite doubles as the "no LED platform"
 * compile-compatibility check: it builds and runs correctly with no
 * `PICO_DEFAULT_LED_PIN` in scope, matching how @ref led.c behaves on boards
 * that do not define a default LED pin.
 */

#include "unity.h"
#include "driver/led_activity.h"
#include "driver/led_policy.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_policy_starts_off_after_reset(void)
{
    led_policy_state_t state;

    led_policy_reset(&state);

    TEST_ASSERT_FALSE(led_policy_output(&state));
}

void test_policy_manual_toggle_is_independent_of_activity(void)
{
    led_policy_state_t state;

    led_policy_reset(&state);

    led_policy_toggle_manual(&state);
    TEST_ASSERT_TRUE(led_policy_output(&state));

    led_policy_set_usb_activity(&state, true);
    TEST_ASSERT_TRUE(led_policy_output(&state));

    /* Activity clearing must not disturb the still-on manual state. */
    led_policy_set_usb_activity(&state, false);
    TEST_ASSERT_TRUE(led_policy_output(&state));

    /* Manual toggle back off must not disturb activity state. */
    led_policy_set_usb_activity(&state, true);
    led_policy_toggle_manual(&state);
    TEST_ASSERT_TRUE(led_policy_output(&state));
    led_policy_set_usb_activity(&state, false);
    TEST_ASSERT_FALSE(led_policy_output(&state));
}

void test_policy_merges_manual_or_activity(void)
{
    led_policy_state_t state;

    led_policy_reset(&state);
    TEST_ASSERT_FALSE(led_policy_output(&state));

    led_policy_set_usb_activity(&state, true);
    TEST_ASSERT_TRUE(led_policy_output(&state));

    led_policy_toggle_manual(&state);
    TEST_ASSERT_TRUE(led_policy_output(&state));

    led_policy_set_usb_activity(&state, false);
    TEST_ASSERT_TRUE(led_policy_output(&state));

    led_policy_toggle_manual(&state);
    TEST_ASSERT_FALSE(led_policy_output(&state));
}

void test_activity_window_starts_closed(void)
{
    led_activity_window_t window;

    led_activity_window_reset(&window);

    TEST_ASSERT_FALSE(led_activity_window_poll(&window, 0u));
    TEST_ASSERT_FALSE(led_activity_window_poll(&window, 1000000u));
}

void test_activity_window_open_until_deadline(void)
{
    led_activity_window_t window;

    led_activity_window_reset(&window);
    led_activity_window_note(&window, 1000u, 500u);

    TEST_ASSERT_TRUE(led_activity_window_poll(&window, 1000u));
    TEST_ASSERT_TRUE(led_activity_window_poll(&window, 1499u));
}

void test_activity_window_times_out_and_clears(void)
{
    led_activity_window_t window;

    led_activity_window_reset(&window);
    led_activity_window_note(&window, 1000u, 500u);

    TEST_ASSERT_FALSE(led_activity_window_poll(&window, 1500u));
    /* Once expired, later polls stay closed without re-arming. */
    TEST_ASSERT_FALSE(led_activity_window_poll(&window, 1600u));
}

void test_activity_window_repeated_activity_extends_deadline(void)
{
    led_activity_window_t window;

    led_activity_window_reset(&window);
    led_activity_window_note(&window, 1000u, 500u);
    TEST_ASSERT_TRUE(led_activity_window_poll(&window, 1400u));

    /* New activity before expiry extends the window from the new "now". */
    led_activity_window_note(&window, 1400u, 500u);
    TEST_ASSERT_TRUE(led_activity_window_poll(&window, 1500u));
    TEST_ASSERT_TRUE(led_activity_window_poll(&window, 1899u));
    TEST_ASSERT_FALSE(led_activity_window_poll(&window, 1900u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_policy_starts_off_after_reset);
    RUN_TEST(test_policy_manual_toggle_is_independent_of_activity);
    RUN_TEST(test_policy_merges_manual_or_activity);
    RUN_TEST(test_activity_window_starts_closed);
    RUN_TEST(test_activity_window_open_until_deadline);
    RUN_TEST(test_activity_window_times_out_and_clears);
    RUN_TEST(test_activity_window_repeated_activity_extends_deadline);
    return UNITY_END();
}
