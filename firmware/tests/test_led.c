/**
 * @file test_led.c
 * @brief Real identification LED driver tests with and without a board LED.
 */

#include "unity.h"
#include "platform/led.h"
#include "hardware/gpio.h"

static unsigned int gpio_calls;
static bool output_on;
static bool output_enabled;

void gpio_init(unsigned int gpio)
{
    (void)gpio;
#ifdef PICO_DEFAULT_LED_PIN
    TEST_ASSERT_EQUAL_UINT(PICO_DEFAULT_LED_PIN, gpio);
#endif
    gpio_calls += 1u;
}

void gpio_put(unsigned int gpio, bool value)
{
    (void)gpio;
#ifdef PICO_DEFAULT_LED_PIN
    TEST_ASSERT_EQUAL_UINT(PICO_DEFAULT_LED_PIN, gpio);
#endif
    output_on = value;
    gpio_calls += 1u;
}

void gpio_set_dir(unsigned int gpio, bool output)
{
    (void)gpio;
#ifdef PICO_DEFAULT_LED_PIN
    TEST_ASSERT_EQUAL_UINT(PICO_DEFAULT_LED_PIN, gpio);
#endif
    TEST_ASSERT_FALSE(output_on);
    output_enabled = output;
    gpio_calls += 1u;
}

void setUp(void)
{
    gpio_calls = 0u;
    output_on = false;
    output_enabled = false;
    led_init();
}

void tearDown(void)
{
}

void test_initial_state_is_off_and_null_reads_fail(void)
{
    bool on = true;
    bool available = led_read_state(&on);
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_FALSE(led_read_state(NULL));
#ifdef PICO_DEFAULT_LED_PIN
    TEST_ASSERT_TRUE(available);
    TEST_ASSERT_TRUE(output_enabled);
    TEST_ASSERT_EQUAL_UINT(3u, gpio_calls);
#else
    TEST_ASSERT_FALSE(available);
    TEST_ASSERT_FALSE(output_enabled);
    TEST_ASSERT_EQUAL_UINT(0u, gpio_calls);
#endif
}

void test_set_on_off_is_idempotent_and_matches_output(void)
{
    bool on = false;
    led_set(true);
    led_set(true);
#ifdef PICO_DEFAULT_LED_PIN
    TEST_ASSERT_TRUE(led_read_state(&on));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(output_on);
#else
    TEST_ASSERT_FALSE(led_read_state(&on));
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_EQUAL_UINT(0u, gpio_calls);
#endif
    led_set(false);
    (void)led_read_state(&on);
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_FALSE(output_on);
}

void test_reinitialize_returns_identification_to_off(void)
{
    bool on = true;
    led_set(true);
    led_init();
    (void)led_read_state(&on);
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_FALSE(output_on);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_off_and_null_reads_fail);
    RUN_TEST(test_set_on_off_is_idempotent_and_matches_output);
    RUN_TEST(test_reinitialize_returns_identification_to_off);
    return UNITY_END();
}