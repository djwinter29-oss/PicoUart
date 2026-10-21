/**
 * @file test_board_config.c
 * @brief Corruption, profile compatibility, and default/recovery boot decisions.
 */
#include "unity.h"
#include "platform/board_config.h"

#include <string.h>

static uint8_t sector[BOARD_CONFIG_SECTOR_SIZE];

void setUp(void) { memset(sector, 0xff, sizeof(sector)); }
void tearDown(void) {}

static void put_u32(size_t offset, uint32_t value)
{
    for (unsigned int index = 0; index < 4; ++index) {
        sector[offset + index] = (uint8_t)(value >> (index * 8));
    }
}

static void checksum(void)
{
    uint8_t protected[32];
    memcpy(protected, sector, 12);
    memcpy(protected + 12, sector + 16, 20);
    put_u32(12, board_config_crc32(protected, sizeof(protected)));
}

static void record(uint32_t profile)
{
    memset(sector, 0, BOARD_CONFIG_RECORD_SIZE);
    memcpy(sector, "PUCF", 4);
    sector[4] = 1;
    sector[6] = 32;
    put_u32(8, BOARD_CONFIG_RECORD_SIZE);
    put_u32(32, profile);
    checksum();
}

static void assert_default(board_config_reason_t reason)
{
    board_config_status_t status = board_config_decode(sector, sizeof(sector), false, false);
    TEST_ASSERT_EQUAL(BOARD_CONFIG_DEFAULTS, status.source);
    TEST_ASSERT_EQUAL(reason, status.reason);
    TEST_ASSERT_EQUAL(BOARD_CLOCK_DEFAULT, status.active_profile);
    TEST_ASSERT_EQUAL_UINT32(BOARD_CONFIG_PROFILE_UNAVAILABLE, status.stored_profile);
}

static void test_crc_and_blank_defaults(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xcbf43926u, board_config_crc32((const uint8_t *)"123456789", 9));
    TEST_ASSERT_EQUAL_HEX32(0, board_config_crc32((const uint8_t *)"", 0));
    assert_default(BOARD_CONFIG_ERASED);
    TEST_ASSERT_EQUAL(BOARD_CONFIG_INVALID_HEADER, board_config_decode(NULL, 0, false, false).reason);
    TEST_ASSERT_EQUAL(BOARD_CONFIG_INVALID_HEADER, board_config_decode(sector, 35, false, false).reason);
}

static void test_profiles_are_exact_and_target_checked(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, board_config_clock_khz(BOARD_CLOCK_DEFAULT));
    TEST_ASSERT_EQUAL_UINT32(250000, board_config_clock_khz(BOARD_CLOCK_250_MHZ));
    TEST_ASSERT_EQUAL_UINT32(280000, board_config_clock_khz(BOARD_CLOCK_280_MHZ));
    for (unsigned int family = 0; family < 2; ++family) {
        for (uint32_t profile = 0; profile < 4; ++profile) {
            record(profile);
            board_config_status_t status = board_config_decode(sector, sizeof(sector), family != 0, false);
            bool supported = profile == 0 || profile == (family ? 2u : 1u);
            TEST_ASSERT_EQUAL(supported ? BOARD_CONFIG_OK : BOARD_CONFIG_INVALID_PROFILE, status.reason);
            TEST_ASSERT_EQUAL(supported ? BOARD_CONFIG_STORED : BOARD_CONFIG_DEFAULTS, status.source);
            TEST_ASSERT_EQUAL(supported ? profile : 0, status.active_profile);
        }
    }
}

static void test_all_record_corruption_reverts_to_defaults(void)
{
    for (size_t offset = 0; offset < BOARD_CONFIG_RECORD_SIZE; ++offset) {
        record(BOARD_CLOCK_250_MHZ);
        sector[offset] ^= 0x80;
        board_config_status_t status = board_config_decode(sector, sizeof(sector), false, false);
        TEST_ASSERT_NOT_EQUAL(BOARD_CONFIG_OK, status.reason);
        TEST_ASSERT_EQUAL(BOARD_CLOCK_DEFAULT, status.active_profile);
    }
    record(0);
    sector[16] = 1;
    checksum();
    assert_default(BOARD_CONFIG_INVALID_HEADER);
    record(0);
    put_u32(8, UINT32_MAX);
    assert_default(BOARD_CONFIG_INVALID_HEADER);
    record(0);
    sector[4] = 2;
    checksum();
    assert_default(BOARD_CONFIG_UNSUPPORTED_VERSION);
    record(UINT32_MAX);
    assert_default(BOARD_CONFIG_INVALID_PROFILE);
}

static void test_interrupted_programming_and_recovery(void)
{
    uint8_t valid[BOARD_CONFIG_RECORD_SIZE];
    record(1);
    memcpy(valid, sector, sizeof(valid));
    for (size_t count = 0; count < sizeof(valid); ++count) {
        memset(sector, 0xff, sizeof(sector));
        memcpy(sector, valid, count);
        board_config_status_t status = board_config_decode(sector, sizeof(sector), false, false);
        TEST_ASSERT_EQUAL(BOARD_CLOCK_DEFAULT, status.active_profile);
    }
    record(1);
    board_config_status_t status = board_config_decode(sector, sizeof(sector), false, true);
    TEST_ASSERT_EQUAL(BOARD_CONFIG_RECOVERY, status.source);
    TEST_ASSERT_EQUAL(BOARD_CONFIG_WATCHDOG_RECOVERY, status.reason);
    TEST_ASSERT_EQUAL(1, status.stored_profile);
    TEST_ASSERT_EQUAL(BOARD_CLOCK_DEFAULT, status.active_profile);
    record(0);
    status = board_config_decode(sector, sizeof(sector), false, true);
    TEST_ASSERT_EQUAL(BOARD_CONFIG_STORED, status.source);
    TEST_ASSERT_EQUAL(BOARD_CONFIG_OK, status.reason);
}

static void test_encoder_matches_provisioned_records_and_checks_tail(void)
{
    uint8_t expected[BOARD_CONFIG_RECORD_SIZE];
    for (uint32_t profile = 0u; profile < 3u; ++profile) {
        record(profile);
        memcpy(expected, sector, sizeof(expected));
        memset(sector, 0xff, sizeof(sector));
        board_config_encode(sector, (board_clock_profile_t)profile);
        TEST_ASSERT_EQUAL_MEMORY(expected, sector, sizeof(expected));
        board_config_status_t status = board_config_decode(sector, sizeof(sector), profile == 2u, false);
        TEST_ASSERT_EQUAL(BOARD_CONFIG_OK, status.reason);
        TEST_ASSERT_EQUAL_UINT32(profile, status.stored_profile);
    }
    sector[sizeof(sector) - 1u] = 0;
    TEST_ASSERT_EQUAL(BOARD_CONFIG_INVALID_HEADER, board_config_decode(sector, sizeof(sector), true, false).reason);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_and_blank_defaults);
    RUN_TEST(test_profiles_are_exact_and_target_checked);
    RUN_TEST(test_all_record_corruption_reverts_to_defaults);
    RUN_TEST(test_interrupted_programming_and_recovery);
    RUN_TEST(test_encoder_matches_provisioned_records_and_checks_tail);
    return UNITY_END();
}
