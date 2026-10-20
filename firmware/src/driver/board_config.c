/**
 * @file board_config.c
 * @brief Version-1 record encoding and bounded decoding with complete-default fallback.
 */
#include "driver/board_config.h"

#include <string.h>

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

uint32_t board_config_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < size; ++index) {
        crc = crc_byte(crc, data[index]);
    }
    return crc ^ UINT32_MAX;
}

uint32_t board_config_clock_khz(board_clock_profile_t profile)
{
    switch (profile) {
    case BOARD_CLOCK_250_MHZ: return 250000u;
    case BOARD_CLOCK_280_MHZ: return 280000u;
    default: return 0u;
    }
}

/** @copydoc board_config_profile_supported */
bool board_config_profile_supported(uint32_t profile, bool rp2350)
{
    return profile == BOARD_CLOCK_DEFAULT || profile == (rp2350 ? BOARD_CLOCK_280_MHZ : BOARD_CLOCK_250_MHZ);
}

/** @copydoc board_config_encode */
void board_config_encode(uint8_t record[BOARD_CONFIG_RECORD_SIZE], board_clock_profile_t profile)
{
    memset(record, 0, BOARD_CONFIG_RECORD_SIZE);
    memcpy(record, "PUCF", 4u);
    record[4] = 1u;
    record[6] = 32u;
    record[8] = BOARD_CONFIG_RECORD_SIZE;
    record[32] = (uint8_t)profile;
    uint8_t protected[BOARD_CONFIG_RECORD_SIZE - 4u];
    memcpy(protected, record, 12u);
    memcpy(protected + 12u, record + 16u, BOARD_CONFIG_RECORD_SIZE - 16u);
    uint32_t crc = board_config_crc32(protected, sizeof(protected));
    for (unsigned int index = 0; index < 4u; ++index) {
        record[12u + index] = (uint8_t)(crc >> (8u * index));
    }
}

board_config_status_t board_config_decode(const uint8_t *sector, size_t size, bool rp2350, bool recovery)
{
    board_config_status_t status = {
        .source = BOARD_CONFIG_DEFAULTS,
        .reason = BOARD_CONFIG_INVALID_HEADER,
        .stored_profile = BOARD_CONFIG_PROFILE_UNAVAILABLE,
        .active_profile = BOARD_CLOCK_DEFAULT,
    };
    if (sector == NULL || size != BOARD_CONFIG_SECTOR_SIZE) {
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
        status.reason = BOARD_CONFIG_ERASED;
        return status;
    }
    if (memcmp(sector, "PUCF", 4u) != 0) {
        return status;
    }
    if (sector[4] != 1u || sector[5] != 0u) {
        status.reason = BOARD_CONFIG_UNSUPPORTED_VERSION;
        return status;
    }
    if (sector[6] != 32u || sector[7] != 0u || read_u32(sector + 8u) != BOARD_CONFIG_RECORD_SIZE) {
        return status;
    }
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < BOARD_CONFIG_RECORD_SIZE; ++index) {
        if (index < 12u || index >= 16u) {
            crc = crc_byte(crc, sector[index]);
        }
    }
    if ((crc ^ UINT32_MAX) != read_u32(sector + 12u)) {
        status.reason = BOARD_CONFIG_CRC_FAILED;
        return status;
    }
    for (size_t index = 16u; index < 32u; ++index) {
        if (sector[index] != 0u) {
            return status;
        }
    }
    for (size_t index = BOARD_CONFIG_RECORD_SIZE; index < size; ++index) {
        if (sector[index] != 0xffu) {
            return status;
        }
    }
    uint32_t profile = read_u32(sector + 32u);
    if (!board_config_profile_supported(profile, rp2350)) {
        status.reason = BOARD_CONFIG_INVALID_PROFILE;
        return status;
    }
    status.stored_profile = profile;
    status.active_profile = (board_clock_profile_t)profile;
    status.source = BOARD_CONFIG_STORED;
    status.reason = BOARD_CONFIG_OK;
    if (recovery && profile != BOARD_CLOCK_DEFAULT) {
        status.active_profile = BOARD_CLOCK_DEFAULT;
        status.source = BOARD_CONFIG_RECOVERY;
        status.reason = BOARD_CONFIG_WATCHDOG_RECOVERY;
    }
    return status;
}
