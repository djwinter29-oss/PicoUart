/**
 * @file test_temperature.c
 * @brief Unity tests for SDK-free temperature report conversion.
 */

#include "unity.h"

#include "driver/temperature.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_temperature_conversion_truncates_centidegrees(void)
{
    TEST_ASSERT_EQUAL_INT16(2530, temperature_to_hid_centidegrees(25.30f));
    TEST_ASSERT_EQUAL_INT16(-401, temperature_to_hid_centidegrees(-4.019f));
}

void test_temperature_conversion_saturates_hid_range(void)
{
    TEST_ASSERT_EQUAL_INT16(TEMPERATURE_HID_MAX_CENTIDEGREES,
                            temperature_to_hid_centidegrees(400.0f));
    TEST_ASSERT_EQUAL_INT16(TEMPERATURE_HID_MIN_CENTIDEGREES,
                            temperature_to_hid_centidegrees(-400.0f));
}

void test_temperature_conversion_rejects_non_finite_values(void)
{
    TEST_ASSERT_EQUAL_INT16(0, temperature_to_hid_centidegrees(NAN));
    TEST_ASSERT_EQUAL_INT16(0, temperature_to_hid_centidegrees(INFINITY));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_temperature_conversion_truncates_centidegrees);
    RUN_TEST(test_temperature_conversion_saturates_hid_range);
    RUN_TEST(test_temperature_conversion_rejects_non_finite_values);
    return UNITY_END();
}