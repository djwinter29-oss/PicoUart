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
    boot_config_load(false);
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
    boot_config_status_t status = boot_config_decode(sector, sizeof(sector), false);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_DEFAULTS, status.source);
    TEST_ASSERT_EQUAL(reason, status.reason);
    TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, status.active_profile);
    TEST_ASSERT_EQUAL_UINT32(BOOT_CONFIG_PROFILE_UNAVAILABLE, status.stored_profile);
}

static void test_load_save_and_private_status_cache(void)
{
    uint32_t profile = PICO_RP2350 ? BOOT_CLOCK_280_MHZ : BOOT_CLOCK_250_MHZ;
    boot_config_t config = {.clock_profile = (boot_clock_profile_t)profile};
    TEST_ASSERT_EQUAL(BOOT_CONFIG_ERASED, boot_config_get_status()->reason);
    TEST_ASSERT_TRUE(boot_config_set(&config));
    TEST_ASSERT_TRUE(boot_config_save());
    TEST_ASSERT_EQUAL_UINT32(1u, erase_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, program_calls);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_ERASED, boot_config_get_status()->reason);

    boot_config_load(false);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_STORED, boot_config_get_status()->source);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_OK, boot_config_get_status()->reason);
    TEST_ASSERT_EQUAL(profile, boot_config_get_status()->active_profile);
    TEST_ASSERT_EQUAL_UINT32(profile, boot_config_get_status()->stored_profile);
    TEST_ASSERT_EQUAL(config.clock_profile, boot_config_get()->clock_profile);

    TEST_ASSERT_TRUE(boot_config_save());
    TEST_ASSERT_EQUAL_UINT32(1u, erase_calls);
    boot_config_t unsupported = {.clock_profile = (boot_clock_profile_t)UINT32_MAX};
    TEST_ASSERT_FALSE(boot_config_set(&unsupported));
    TEST_ASSERT_FALSE(boot_config_set(NULL));
    TEST_ASSERT_EQUAL(config.clock_profile, boot_config_get()->clock_profile);
    TEST_ASSERT_EQUAL_UINT32(1u, erase_calls);

    config.clock_profile = BOOT_CLOCK_DEFAULT;
    TEST_ASSERT_TRUE(boot_config_set(&config));
    TEST_ASSERT_EQUAL(profile, boot_config_get_status()->active_profile);
    flash_result = -1;
    TEST_ASSERT_FALSE(boot_config_save());
    TEST_ASSERT_EQUAL_UINT32(1u, erase_calls);

    flash_result = PICO_OK;
    corrupt_flash_tail = true;
    TEST_ASSERT_FALSE(boot_config_save());
    TEST_ASSERT_EQUAL_UINT32(2u, erase_calls);
    boot_config_load(false);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_DEFAULTS, boot_config_get_status()->source);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_INVALID_HEADER, boot_config_get_status()->reason);
    TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, boot_config_get()->clock_profile);
    TEST_ASSERT_EQUAL_UINT32(2u, erase_calls);
}

static void test_additional_profiles_save_and_load(void)
{
    const boot_clock_profile_t profiles[] = {BOOT_CLOCK_150_MHZ, BOOT_CLOCK_200_MHZ, BOOT_CLOCK_250_MHZ};
    for (size_t index = 0u; index < sizeof(profiles) / sizeof(profiles[0]); ++index) {
        boot_config_t config = {.clock_profile = profiles[index]};
        TEST_ASSERT_TRUE(boot_config_set(&config));
        TEST_ASSERT_TRUE(boot_config_save());
        TEST_ASSERT_EQUAL_UINT32(index + 1u, erase_calls);
        TEST_ASSERT_EQUAL_UINT32(index + 1u, program_calls);
        boot_config_load(false);
        TEST_ASSERT_EQUAL(profiles[index], boot_config_get()->clock_profile);
        TEST_ASSERT_EQUAL(profiles[index], boot_config_get_status()->stored_profile);
        TEST_ASSERT_EQUAL(profiles[index], boot_config_get_status()->active_profile);
    }
}

static void test_crc_and_blank_defaults(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xcbf43926u, boot_config_crc32((const uint8_t *)"123456789", 9));
    TEST_ASSERT_EQUAL_HEX32(0, boot_config_crc32((const uint8_t *)"", 0));
    assert_default(BOOT_CONFIG_ERASED);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_INVALID_HEADER, boot_config_decode(NULL, 0, false).reason);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_INVALID_HEADER, boot_config_decode(sector, 35, false).reason);
}

static void test_profiles_are_exact_and_target_checked(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, boot_config_clock_khz(BOOT_CLOCK_DEFAULT));
    TEST_ASSERT_EQUAL_UINT32(250000, boot_config_clock_khz(BOOT_CLOCK_250_MHZ));
    TEST_ASSERT_EQUAL_UINT32(280000, boot_config_clock_khz(BOOT_CLOCK_280_MHZ));
    TEST_ASSERT_EQUAL_UINT32(200000, boot_config_clock_khz(BOOT_CLOCK_200_MHZ));
    TEST_ASSERT_EQUAL_UINT32(150000, boot_config_clock_khz(BOOT_CLOCK_150_MHZ));
    const uint32_t invalid_profiles[] = {5u, 255u, UINT32_MAX};
    for (size_t index = 0u; index < sizeof(invalid_profiles) / sizeof(invalid_profiles[0]); ++index) {
        boot_config_t invalid = {.clock_profile = (boot_clock_profile_t)invalid_profiles[index]};
        TEST_ASSERT_FALSE(boot_config_profile_supported(invalid_profiles[index]));
        TEST_ASSERT_EQUAL_UINT32(0u, boot_config_clock_khz(invalid.clock_profile));
        TEST_ASSERT_FALSE(boot_config_set(&invalid));
        TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, boot_config_get()->clock_profile);
    }
    for (uint32_t profile = 0; profile < 6; ++profile) {
        record(profile);
        boot_config_status_t status = boot_config_decode(sector, sizeof(sector), false);
        bool supported = profile <= 3u || (PICO_RP2350 && profile == 4u);
        TEST_ASSERT_EQUAL(supported ? BOOT_CONFIG_OK : BOOT_CONFIG_INVALID_PROFILE, status.reason);
        TEST_ASSERT_EQUAL(supported ? BOOT_CONFIG_STORED : BOOT_CONFIG_DEFAULTS, status.source);
        TEST_ASSERT_EQUAL(supported ? profile : 0, status.active_profile);
    }
}

static void test_all_record_corruption_reverts_to_defaults(void)
{
    for (size_t offset = 0; offset < BOOT_CONFIG_RECORD_SIZE; ++offset) {
        record(PICO_RP2350 ? BOOT_CLOCK_280_MHZ : BOOT_CLOCK_250_MHZ);
        sector[offset] ^= 0x80;
        boot_config_status_t status = boot_config_decode(sector, sizeof(sector), false);
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
    uint32_t profile = PICO_RP2350 ? BOOT_CLOCK_280_MHZ : BOOT_CLOCK_250_MHZ;
    record(profile);
    memcpy(valid, sector, sizeof(valid));
    for (size_t count = 0; count < sizeof(valid); ++count) {
        memset(sector, 0xff, sizeof(sector));
        memcpy(sector, valid, count);
        boot_config_status_t status = boot_config_decode(sector, sizeof(sector), false);
        TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, status.active_profile);
    }
    record(profile);
    boot_config_status_t status = boot_config_decode(sector, sizeof(sector), true);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_RECOVERY, status.source);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_WATCHDOG_RECOVERY, status.reason);
    TEST_ASSERT_EQUAL(profile, status.stored_profile);
    TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, status.active_profile);
    boot_config_load(true);
    TEST_ASSERT_EQUAL(profile, boot_config_get()->clock_profile);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_RECOVERY, boot_config_get_status()->source);
    TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, boot_config_get_status()->active_profile);
    TEST_ASSERT_EQUAL_UINT32(0u, erase_calls);
    TEST_ASSERT_EQUAL_UINT32(0u, program_calls);
    record(0);
    status = boot_config_decode(sector, sizeof(sector), true);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_STORED, status.source);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_OK, status.reason);
}

static void test_encoder_matches_provisioned_records_and_checks_tail(void)
{
    uint8_t expected[BOOT_CONFIG_RECORD_SIZE];
    for (uint32_t profile = 0u; profile < 5u; ++profile) {
        record(profile);
        memcpy(expected, sector, sizeof(expected));
        memset(sector, 0xff, sizeof(sector));
        boot_config_encode(sector, (boot_clock_profile_t)profile);
        TEST_ASSERT_EQUAL_MEMORY(expected, sector, sizeof(expected));
        boot_config_status_t status = boot_config_decode(sector, sizeof(sector), false);
        if (boot_config_profile_supported(profile)) {
            TEST_ASSERT_EQUAL(BOOT_CONFIG_OK, status.reason);
            TEST_ASSERT_EQUAL_UINT32(profile, status.stored_profile);
        } else {
            TEST_ASSERT_EQUAL(BOOT_CONFIG_INVALID_PROFILE, status.reason);
        }
    }
    sector[sizeof(sector) - 1u] = 0;
    TEST_ASSERT_EQUAL(BOOT_CONFIG_INVALID_HEADER, boot_config_decode(sector, sizeof(sector), false).reason);
}

static void test_crc_failure_uses_defaults_without_writing_flash(void)
{
    record(0u);
    sector[32] ^= 0x80u;
    uint8_t original[BOOT_CONFIG_RECORD_SIZE];
    memcpy(original, sector, sizeof(original));
    boot_config_load(false);
    TEST_ASSERT_EQUAL(BOOT_CONFIG_CRC_FAILED, boot_config_get_status()->reason);
    TEST_ASSERT_EQUAL(BOOT_CLOCK_DEFAULT, boot_config_get()->clock_profile);
    TEST_ASSERT_EQUAL_UINT32(0u, erase_calls);
    TEST_ASSERT_EQUAL_UINT32(0u, program_calls);
    TEST_ASSERT_EQUAL_MEMORY(original, sector, sizeof(original));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_save_and_private_status_cache);
    RUN_TEST(test_additional_profiles_save_and_load);
    RUN_TEST(test_crc_failure_uses_defaults_without_writing_flash);
    RUN_TEST(test_crc_and_blank_defaults);
    RUN_TEST(test_profiles_are_exact_and_target_checked);
    RUN_TEST(test_all_record_corruption_reverts_to_defaults);
    RUN_TEST(test_interrupted_programming_and_recovery);
    RUN_TEST(test_encoder_matches_provisioned_records_and_checks_tail);
    return UNITY_END();
}
