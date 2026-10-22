/**
 * @file boot_config.c
 * @brief Private settings cache, persistent storage, and read-only boot diagnostics.
 */
#include "platform/boot_config.h"

#include "config/capacity_config.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include <string.h>

/** @brief Reserved flash erase-sector size in bytes. */
#define BOOT_CONFIG_SECTOR_SIZE 4096u
/** @brief Version-1 logical record size; remaining sector bytes stay erased. */
#define BOOT_CONFIG_RECORD_SIZE 36u

_Static_assert(PICO_FLASH_SIZE_BYTES == PICO_UART_CONFIG_FLASH_SIZE, "Configuration must use physical board capacity");
_Static_assert(PICO_UART_CONFIG_FLASH_OFFSET + BOOT_CONFIG_SECTOR_SIZE == PICO_FLASH_SIZE_BYTES,
               "Configuration must occupy the final flash sector");

/** @brief Requested settings, separate from immutable boot diagnostics. */
static boot_config_t boot_config_settings;

/** @brief Target family selected by the SDK, never supplied by callers. */
static const bool boot_config_rp2350 =
#if defined(PICO_RP2350A) || defined(PICO_RP2350B) || (defined(PICO_RP2350) && PICO_RP2350)
    true;
#else
    false;
#endif

/** @brief Frequency and supported targets for one clock-profile ID. */
typedef struct {
    uint32_t clock_khz; /**< Exact frequency request, or zero to retain the SDK clock. */
    bool rp2040;       /**< Profile is permitted on RP2040 targets. */
    bool rp2350;       /**< Profile is permitted on RP2350 targets. */
} boot_clock_definition_t;

/** @brief Profile definitions indexed by the settings/storage/HID enum. */
static const boot_clock_definition_t boot_clock_definitions[] = {
    [BOOT_CLOCK_DEFAULT] = {.clock_khz = 0u, .rp2040 = true, .rp2350 = true},
    [BOOT_CLOCK_150_MHZ] = {.clock_khz = 150000u, .rp2040 = true, .rp2350 = true},
    [BOOT_CLOCK_200_MHZ] = {.clock_khz = 200000u, .rp2040 = true, .rp2350 = true},
    [BOOT_CLOCK_250_MHZ] = {.clock_khz = 250000u, .rp2040 = true, .rp2350 = true},
    [BOOT_CLOCK_280_MHZ] = {.clock_khz = 280000u, .rp2040 = false, .rp2350 = true},
};

/** @brief Boot decision cached for startup and read-only diagnostics. */
static boot_config_status_t boot_config_status = {
    .source = BOOT_CONFIG_DEFAULTS,
    .reason = BOOT_CONFIG_ERASED,
    .stored_profile = BOOT_CONFIG_PROFILE_UNAVAILABLE,
    .active_profile = BOOT_CLOCK_DEFAULT,
};

/** @brief Decode an unaligned little-endian 32-bit protocol field. */
static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

/** @brief Update the reflected CRC accumulator by one byte. */
static uint32_t crc_byte(uint32_t crc, uint8_t value)
{
    crc ^= value;
    for (unsigned int bit = 0; bit < 8u; ++bit) {
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

/** @brief Calculate reflected CRC-32/ISO-HDLC for the protected record bytes. */
static uint32_t boot_config_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < size; ++index) {
        crc = crc_byte(crc, data[index]);
    }
    return crc ^ UINT32_MAX;
}

uint32_t boot_config_clock_khz(boot_clock_profile_t profile)
{
    uint32_t index = (uint32_t)profile;
    return index < sizeof(boot_clock_definitions) / sizeof(boot_clock_definitions[0]) ?
        boot_clock_definitions[index].clock_khz : 0u;
}

/** @copydoc boot_config_profile_supported */
bool boot_config_profile_supported(uint32_t profile)
{
    if (profile >= sizeof(boot_clock_definitions) / sizeof(boot_clock_definitions[0])) {
        return false;
    }
    return boot_config_rp2350 ? boot_clock_definitions[profile].rp2350 : boot_clock_definitions[profile].rp2040;
}

/** @brief Encode a version-1 profile record without exposing its layout to callers. */
static void boot_config_encode(uint8_t record[BOOT_CONFIG_RECORD_SIZE], boot_clock_profile_t profile)
{
    memset(record, 0, BOOT_CONFIG_RECORD_SIZE);
    memcpy(record, "PUCF", 4u);
    record[4] = 1u;
    record[6] = 32u;
    record[8] = BOOT_CONFIG_RECORD_SIZE;
    record[32] = (uint8_t)profile;
    uint8_t protected[BOOT_CONFIG_RECORD_SIZE - 4u];
    memcpy(protected, record, 12u);
    memcpy(protected + 12u, record + 16u, BOOT_CONFIG_RECORD_SIZE - 16u);
    uint32_t crc = boot_config_crc32(protected, sizeof(protected));
    for (unsigned int index = 0; index < 4u; ++index) {
        record[12u + index] = (uint8_t)(crc >> (8u * index));
    }
}

/** @brief Validate the complete storage sector and select safe boot diagnostics without writes. */
static boot_config_status_t boot_config_decode(const uint8_t *sector, size_t size, bool recovery)
{
    boot_config_status_t status = {
        .source = BOOT_CONFIG_DEFAULTS,
        .reason = BOOT_CONFIG_INVALID_HEADER,
        .stored_profile = BOOT_CONFIG_PROFILE_UNAVAILABLE,
        .active_profile = BOOT_CLOCK_DEFAULT,
    };
    if (sector == NULL || size != BOOT_CONFIG_SECTOR_SIZE) {
        return status;
    }
    bool erased = true;
    for (size_t index = 0; index < size; ++index) {
        if (sector[index] != 0xffu) {
            erased = false;
            break;
        }
    }
    if (erased) {
        status.reason = BOOT_CONFIG_ERASED;
        return status;
    }
    if (memcmp(sector, "PUCF", 4u) != 0) {
        return status;
    }
    if (sector[4] != 1u || sector[5] != 0u) {
        status.reason = BOOT_CONFIG_UNSUPPORTED_VERSION;
        return status;
    }
    if (sector[6] != 32u || sector[7] != 0u || read_u32(sector + 8u) != BOOT_CONFIG_RECORD_SIZE) {
        return status;
    }
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < BOOT_CONFIG_RECORD_SIZE; ++index) {
        if (index < 12u || index >= 16u) {
            crc = crc_byte(crc, sector[index]);
        }
    }
    if ((crc ^ UINT32_MAX) != read_u32(sector + 12u)) {
        status.reason = BOOT_CONFIG_CRC_FAILED;
        return status;
    }
    for (size_t index = 16u; index < 32u; ++index) {
        if (sector[index] != 0u) {
            return status;
        }
    }
    for (size_t index = BOOT_CONFIG_RECORD_SIZE; index < size; ++index) {
        if (sector[index] != 0xffu) {
            return status;
        }
    }
    uint32_t profile = read_u32(sector + 32u);
    if (!boot_config_profile_supported(profile)) {
        status.reason = BOOT_CONFIG_INVALID_PROFILE;
        return status;
    }
    status.stored_profile = profile;
    status.active_profile = (boot_clock_profile_t)profile;
    status.source = BOOT_CONFIG_STORED;
    status.reason = BOOT_CONFIG_OK;
    if (recovery && profile != BOOT_CLOCK_DEFAULT) {
        status.active_profile = BOOT_CLOCK_DEFAULT;
        status.source = BOOT_CONFIG_RECOVERY;
        status.reason = BOOT_CONFIG_WATCHDOG_RECOVERY;
    }
    return status;
}

/** @copydoc boot_config_load */
void boot_config_load(bool recovery)
{
    const uint8_t *sector = (const uint8_t *)(XIP_BASE + PICO_UART_CONFIG_FLASH_OFFSET);
    boot_config_status = boot_config_decode(sector, BOOT_CONFIG_SECTOR_SIZE, recovery);
    boot_config_settings.clock_profile = boot_config_status.stored_profile == BOOT_CONFIG_PROFILE_UNAVAILABLE ?
        BOOT_CLOCK_DEFAULT : (boot_clock_profile_t)boot_config_status.stored_profile;
}

/** @copydoc boot_config_get */
const boot_config_t *boot_config_get(void)
{
    return &boot_config_settings;
}

/** @copydoc boot_config_get_status */
const boot_config_status_t *boot_config_get_status(void)
{
    return &boot_config_status;
}

/** @copydoc boot_config_set */
bool boot_config_set(const boot_config_t *config)
{
    if ((config == NULL) || !boot_config_profile_supported((uint32_t)config->clock_profile)) {
        return false;
    }
    boot_config_settings = *config;
    return true;
}

/** @copydoc boot_config_clock_unavailable */
void boot_config_clock_unavailable(void)
{
    boot_config_status.active_profile = BOOT_CLOCK_DEFAULT;
    boot_config_status.source = BOOT_CONFIG_DEFAULTS;
    boot_config_status.reason = BOOT_CONFIG_CLOCK_UNAVAILABLE;
}

/**
 * @brief Erase the reserved sector and program its first page while XIP is unavailable.
 * @param page SRAM page prepared before multicore lockout.
 */
static void __not_in_flash_func(boot_config_program)(void *page)
{
    flash_range_erase(PICO_UART_CONFIG_FLASH_OFFSET, BOOT_CONFIG_SECTOR_SIZE);
    flash_range_program(PICO_UART_CONFIG_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
}

/** @copydoc boot_config_save */
bool boot_config_save(void)
{
    return boot_config_save_with_result() == BOOT_CONFIG_SAVE_OK;
}

/** @copydoc boot_config_save_with_result */
boot_config_save_result_t boot_config_save_with_result(void)
{
    boot_clock_profile_t profile = boot_config_settings.clock_profile;

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    boot_config_encode(page, profile);

    const uint8_t *sector = (const uint8_t *)(XIP_BASE + PICO_UART_CONFIG_FLASH_OFFSET);
    boot_config_status_t stored = boot_config_decode(sector, BOOT_CONFIG_SECTOR_SIZE, false);
    if ((stored.reason == BOOT_CONFIG_OK) && (stored.stored_profile == profile)) {
        return BOOT_CONFIG_SAVE_OK;
    }
    if (flash_safe_execute(boot_config_program, page, 1000u) != PICO_OK) {
        return BOOT_CONFIG_SAVE_FLASH_FAILED;
    }
    if (memcmp(sector, page, sizeof(page)) != 0) {
        return BOOT_CONFIG_SAVE_VERIFY_FAILED;
    }

    stored = boot_config_decode(sector, BOOT_CONFIG_SECTOR_SIZE, false);
    return (stored.reason == BOOT_CONFIG_OK) && (stored.stored_profile == profile) ? BOOT_CONFIG_SAVE_OK
                                                                                   : BOOT_CONFIG_SAVE_VERIFY_FAILED;
}
