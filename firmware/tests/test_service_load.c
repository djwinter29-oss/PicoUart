/**
 * @file test_service_load.c
 * @brief Deterministic checks for measured service-pass wall-time ratios.
 */
#include "unity.h"
#include "platform/service_load.h"

void setUp(void)
{
    service_load_init();
}

void tearDown(void)
{
}

static void test_no_sample_until_first_full_window(void)
{
    TEST_ASSERT_EQUAL_UINT16(SERVICE_LOAD_UNAVAILABLE, service_load_read(0u));
    service_load_begin(0u, 100u);
    service_load_end(0u, 1000099u);
    TEST_ASSERT_EQUAL_UINT16(SERVICE_LOAD_UNAVAILABLE, service_load_read(0u));
    service_load_begin(0u, 1000099u);
    service_load_end(0u, 1000100u);
    TEST_ASSERT_EQUAL_UINT16(0u, service_load_read(0u));
}

static void test_exact_ratio_excludes_empty_passes_and_interpass_time(void)
{
    service_load_note(0u); /* Initialization work is not in a measured pass. */
    service_load_begin(0u, 0u);
    service_load_note(0u);
    service_load_note(0u); /* Multiple items do not double-count one pass. */
    service_load_end(0u, 100000u);
    service_load_begin(0u, 200000u);
    service_load_note(0u);
    service_load_end(0u, 350000u);
    service_load_begin(0u, 400000u);
    service_load_end(0u, 1000000u);
    TEST_ASSERT_EQUAL_UINT16(2500u, service_load_read(0u));
    TEST_ASSERT_EQUAL_UINT16(SERVICE_LOAD_UNAVAILABLE, service_load_read(1u));
}

static void test_windows_reset_and_cores_are_independent(void)
{
    service_load_begin(0u, 0u);
    service_load_note(0u);
    service_load_end(0u, 1000000u);
    TEST_ASSERT_EQUAL_UINT16(10000u, service_load_read(0u));
    service_load_begin(1u, 0u);
    service_load_note(1u);
    service_load_end(1u, 500000u);
    service_load_begin(1u, 500000u);
    service_load_end(1u, 1000000u);
    TEST_ASSERT_EQUAL_UINT16(5000u, service_load_read(1u));
    service_load_begin(0u, 1000000u);
    service_load_end(0u, 2000000u);
    TEST_ASSERT_EQUAL_UINT16(0u, service_load_read(0u));
    TEST_ASSERT_EQUAL_UINT16(5000u, service_load_read(1u));
}

static void test_timer_wrap_and_actual_window_duration(void)
{
    uint32_t start = UINT32_MAX - 500000u;
    service_load_begin(0u, start);
    service_load_note(0u);
    service_load_end(0u, start + 300000u);
    service_load_begin(0u, start + 300000u);
    service_load_end(0u, start + 1200000u);
    TEST_ASSERT_EQUAL_UINT16(2500u, service_load_read(0u));
}

static void test_profile_maxima_track_pass_gap_and_timer_wrap(void)
{
    uint32_t start = UINT32_MAX - 20u;
    service_load_begin(1u, start);
    service_load_end(1u, start + 10u);
    service_load_begin(1u, start + 30u);
    service_load_end(1u, start + 70u);
    TEST_ASSERT_EQUAL_UINT32(40u, service_load_max_pass_us[1]);
    TEST_ASSERT_EQUAL_UINT32(30u, service_load_max_gap_us[1]);
    TEST_ASSERT_EQUAL_UINT32(0u, service_load_max_pass_us[0]);
    service_load_init();
    TEST_ASSERT_EQUAL_UINT32(0u, service_load_max_pass_us[1]);
    TEST_ASSERT_EQUAL_UINT32(0u, service_load_max_gap_us[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_profile_maxima_track_pass_gap_and_timer_wrap);
    RUN_TEST(test_no_sample_until_first_full_window);
    RUN_TEST(test_exact_ratio_excludes_empty_passes_and_interpass_time);
    RUN_TEST(test_windows_reset_and_cores_are_independent);
    RUN_TEST(test_timer_wrap_and_actual_window_duration);
    return UNITY_END();
}
