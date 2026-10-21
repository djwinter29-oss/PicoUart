/**
 * @file test_boot_config.c
 * @brief Corruption, profile compatibility, and default/recovery boot decisions.
 */
#include "unity.h"
#include "../src/platform/boot_config.c"

#include <string.h>

uint8_t g_boot_config_flash[4096];

#define sector g_boot_config_flash

static uint32_t erase_calls;
static uint32_t program_calls;
static int flash_result;
static bool corrupt_flash_tail;

void flash_range_erase(uint32_t offset, size_t count)
{
    TEST_ASSERT_EQUAL_UINT32(PICO_UART_CONFIG_FLASH_OFFSET, offset);
    TEST_ASSERT_EQUAL_UINT32(BOOT_CONFIG_SECTOR_SIZE, count);
    memset(sector, 0xff, count);
    ++erase_calls;
}

void flash_range_program(uint32_t offset, const uint8_t *data, size_t count)
{
    TEST_ASSERT_EQUAL_UINT32(PICO_UART_CONFIG_FLASH_OFFSET, offset);
    TEST_ASSERT_EQUAL_UINT(FLASH_PAGE_SIZE, count);
    memcpy(sector, data, count);
    if (corrupt_flash_tail) {
        sector[BOOT_CONFIG_SECTOR_SIZE - 1u] = 0u;
    }
    ++program_calls;
}

int flash_safe_execute(void (*function)(void *), void *param, uint32_t timeout_ms)
{
    TEST_ASSERT_EQUAL_UINT32(1000u, timeout_ms);
    if (flash_result == PICO_OK) {
        function(param);
    }
    return flash_result;
}

void setUp(void)
{
    memset(sector, 0xff, sizeof(sector));
    erase_calls = program_calls = 0u;
    flash_result = PICO_OK;
    corrupt_flash_tail = false;
    boot_config_load(false, false);
}

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
    put_u32(12, boot_config_crc32(protected, sizeof(protected)));
}

static void record(uint32_t profile)
{
    memset(sector, 0, BOOT_CONFIG_RECORD_SIZE);
    memcpy(sector, "PUCF", 4);
    sector[4] = 1;
    sector[6] = 32;
    put_u32(8, BOOT_CONFIG_RECORD_SIZE);
    put_u32(32, profile);
    checksum();
}

static void assert_default(boot_config_reason_t reason)
{
    boot_config_status_t status = boot_config_decode(sector, sizeof(sector), false, false);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_DEFAULTS, status.source);
    TEST_ASSERT_EQUAL(reason, status.reason);
    TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, status.active_profile);
    TEST_ASSERT_EQUAL_UINT32(BOOT_CONFIG_PROFILE_UNAVAILABLE, status.stored_profile);
}

static void test_load_save_and_private_status_cache(void)
{
    TEST_ASSERT_EQUAL(BOOT_CONFIG_ERASED, boot_config_get()->reason);
    TEST_ASSERT_TRUE(boot_config_save(BOOT_CLOCK_250_MHZ, false));
    TEST_ASSERT_EQUAL_UINT32(1u, erase_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, program_calls);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_ERASED, boot_config_get()->reason);

    boot_config_load(false, false);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_STORED, boot_config_get()->source);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_OK, boot_config_get()->reason);
    TEST_ASSERT_EQUAL(BOOT_CLOCK_250_MHZ, boot_config_get()->active_profile);
    TEST_ASSERT_EQUAL_UINT32(BOOT_CLOCK_250_MHZ, boot_config_get()->stored_profile);

    TEST_ASSERT_TRUE(boot_config_save(BOOT_CLOCK_250_MHZ, false));
    TEST_ASSERT_EQUAL_UINT32(1u, erase_calls);
    TEST_ASSERT_FALSE(boot_config_save(BOOT_CLOCK_280_MHZ, false));
    TEST_ASSERT_EQUAL_UINT32(1u, erase_calls);

    flash_result = -1;
    TEST_ASSERT_FALSE(boot_config_save(BOOT_CLOCK_DEFAULT, false));
    TEST_ASSERT_EQUAL_UINT32(1u, erase_calls);

    flash_result = PICO_OK;
    corrupt_flash_tail = true;
    TEST_ASSERT_FALSE(boot_config_save(BOOT_CLOCK_DEFAULT, false));
    TEST_ASSERT_EQUAL_UINT32(2u, erase_calls);
    boot_config_load(false, false);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_DEFAULTS, boot_config_get()->source);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_INVALID_HEADER, boot_config_get()->reason);
}

static void test_crc_and_blank_defaults(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xcbf43926u, boot_config_crc32((const uint8_t *)"123456789", 9));
    TEST_ASSERT_EQUAL_HEX32(0, boot_config_crc32((const uint8_t *)"", 0));
    assert_default(BOOT_CONFIG_ERASED);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_INVALID_HEADER, boot_config_decode(NULL, 0, false, false).reason);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_INVALID_HEADER, boot_config_decode(sector, 35, false, false).reason);
}

static void test_profiles_are_exact_and_target_checked(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, boot_config_clock_khz(BOOT_CLOCK_DEFAULT));
    TEST_ASSERT_EQUAL_UINT32(250000, boot_config_clock_khz(BOOT_CLOCK_250_MHZ));
    TEST_ASSERT_EQUAL_UINT32(280000, boot_config_clock_khz(BOOT_CLOCK_280_MHZ));
    for (unsigned int family = 0; family < 2; ++family) {
        for (uint32_t profile = 0; profile < 4; ++profile) {
            record(profile);
            boot_config_status_t status = boot_config_decode(sector, sizeof(sector), family != 0, false);
            bool supported = profile == 0 || profile == (family ? 2u : 1u);
            TEST_ASSERT_EQUAL(supported ? BOOT_CONFIG_OK : BOOT_CONFIG_INVALID_PROFILE, status.reason);
            TEST_ASSERT_EQUAL(supported ? BOOT_CONFIG_STORED : BOOT_CONFIG_DEFAULTS, status.source);
            TEST_ASSERT_EQUAL(supported ? profile : 0, status.active_profile);
        }
    }
}

static void test_all_record_corruption_reverts_to_defaults(void)
{
    for (size_t offset = 0; offset < BOOT_CONFIG_RECORD_SIZE; ++offset) {
        record(BOOT_CLOCK_250_MHZ);
        sector[offset] ^= 0x80;
        boot_config_status_t status = boot_config_decode(sector, sizeof(sector), false, false);
        TEST_ASSERT_NOT_EQUAL(BOOT_CONFIG_OK, status.reason);
        TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, status.active_profile);
    }
    record(0);
    sector[16] = 1;
    checksum();
    assert_default(BOOT_CONFIG_INVALID_HEADER);
    record(0);
    put_u32(8, UINT32_MAX);
    assert_default(BOOT_CONFIG_INVALID_HEADER);
    record(0);
    sector[4] = 2;
    checksum();
    assert_default(BOOT_CONFIG_UNSUPPORTED_VERSION);
    record(UINT32_MAX);
    assert_default(BOOT_CONFIG_INVALID_PROFILE);
}

static void test_interrupted_programming_and_recovery(void)
{
    uint8_t valid[BOOT_CONFIG_RECORD_SIZE];
    record(1);
    memcpy(valid, sector, sizeof(valid));
    for (size_t count = 0; count < sizeof(valid); ++count) {
        memset(sector, 0xff, sizeof(sector));
        memcpy(sector, valid, count);
        boot_config_status_t status = boot_config_decode(sector, sizeof(sector), false, false);
        TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, status.active_profile);
    }
    record(1);
    boot_config_status_t status = boot_config_decode(sector, sizeof(sector), false, true);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_RECOVERY, status.source);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_WATCHDOG_RECOVERY, status.reason);
    TEST_ASSERT_EQUAL(1, status.stored_profile);
    TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, status.active_profile);
    record(0);
    status = boot_config_decode(sector, sizeof(sector), false, true);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_STORED, status.source);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_OK, status.reason);
}

static void test_encoder_matches_provisioned_records_and_checks_tail(void)
{
    uint8_t expected[BOOT_CONFIG_RECORD_SIZE];
    for (uint32_t profile = 0u; profile < 3u; ++profile) {
        record(profile);
        memcpy(expected, sector, sizeof(expected));
        memset(sector, 0xff, sizeof(sector));
        boot_config_encode(sector, (boot_clock_profile_t)profile);
        TEST_ASSERT_EQUAL_MEMORY(expected, sector, sizeof(expected));
        boot_config_status_t status = boot_config_decode(sector, sizeof(sector), profile == 2u, false);
        TEST_ASSERT_EQUAL(BOOT_CONFIG_OK, status.reason);
        TEST_ASSERT_EQUAL_UINT32(profile, status.stored_profile);
    }
    sector[sizeof(sector) - 1u] = 0;
    TEST_ASSERT_EQUAL(BOOT_CONFIG_INVALID_HEADER, boot_config_decode(sector, sizeof(sector), true, false).reason);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_save_and_private_status_cache);
    RUN_TEST(test_crc_and_blank_defaults);
    RUN_TEST(test_profiles_are_exact_and_target_checked);
    RUN_TEST(test_all_record_corruption_reverts_to_defaults);
    RUN_TEST(test_interrupted_programming_and_recovery);
    RUN_TEST(test_encoder_matches_provisioned_records_and_checks_tail);
    return UNITY_END();
}
