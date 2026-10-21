/**
 * @file boot_config.c
 * @brief Version-1 record encoding and bounded decoding with complete-default fallback.
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
    switch (profile) {
    case BOOT_CLOCK_250_MHZ: return 250000u;
    case BOOT_CLOCK_280_MHZ: return 280000u;
    default: return 0u;
    }
}

/** @copydoc boot_config_profile_supported */
bool boot_config_profile_supported(uint32_t profile, bool rp2350)
{
    return profile == BOOT_CLOCK_DEFAULT || profile == (rp2350 ? BOOT_CLOCK_280_MHZ : BOOT_CLOCK_250_MHZ);
}

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

static boot_config_status_t boot_config_decode(const uint8_t *sector, size_t size, bool rp2350, bool recovery)
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
    if (!boot_config_profile_supported(profile, rp2350)) {
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
void boot_config_load(bool rp2350, bool recovery)
{
    const uint8_t *sector = (const uint8_t *)(XIP_BASE + PICO_UART_CONFIG_FLASH_OFFSET);
    boot_config_status = boot_config_decode(sector, BOOT_CONFIG_SECTOR_SIZE, rp2350, recovery);
}

/** @copydoc boot_config_get */
const boot_config_status_t *boot_config_get(void)
{
    return &boot_config_status;
}

/** @copydoc boot_config_set */
void boot_config_set(const boot_config_status_t *status)
{
    if (status != NULL) {
        boot_config_status = *status;
    }
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
bool boot_config_save(uint32_t profile, bool rp2350)
{
    if (!boot_config_profile_supported(profile, rp2350)) {
        return false;
    }

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    boot_config_encode(page, (boot_clock_profile_t)profile);

    const uint8_t *sector = (const uint8_t *)(XIP_BASE + PICO_UART_CONFIG_FLASH_OFFSET);
    boot_config_status_t stored = boot_config_decode(sector, BOOT_CONFIG_SECTOR_SIZE, rp2350, false);
    if ((stored.reason == BOOT_CONFIG_OK) && (stored.stored_profile == profile)) {
        return true;
    }
    if (flash_safe_execute(boot_config_program, page, 1000u) != PICO_OK) {
        return false;
    }
    if (memcmp(sector, page, sizeof(page)) != 0) {
        return false;
    }

    stored = boot_config_decode(sector, BOOT_CONFIG_SECTOR_SIZE, rp2350, false);
    return (stored.reason == BOOT_CONFIG_OK) && (stored.stored_profile == profile);
}
