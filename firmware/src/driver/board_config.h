/**
 * @file board_config.h
 * @brief Versioned persistent board settings and target-checked clock profiles.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Reserved flash erase-sector size in bytes. */
#define BOARD_CONFIG_SECTOR_SIZE 4096u
/** @brief Version-1 logical record size; remaining sector bytes stay erased. */
#define BOARD_CONFIG_RECORD_SIZE 36u
/** @brief Diagnostic sentinel when no valid stored profile is available. */
#define BOARD_CONFIG_PROFILE_UNAVAILABLE UINT32_MAX

/** @brief Permanent profile IDs; non-default values always identify one exact frequency. */
typedef enum {
    BOARD_CLOCK_DEFAULT = 0, /**< Leave SDK/board startup clock unchanged. */
    BOARD_CLOCK_250_MHZ = 1, /**< Exactly 250 MHz; initially supported on RP2040 only. */
    BOARD_CLOCK_280_MHZ = 2, /**< Exactly 280 MHz; initially supported on RP2350 only. */
} board_clock_profile_t;

/** @brief Source of the active boot settings. */
typedef enum {
    BOARD_CONFIG_DEFAULTS = 0, /**< Defaults because settings are absent or rejected. */
    BOARD_CONFIG_STORED = 1,   /**< Valid stored settings. */
    BOARD_CONFIG_RECOVERY = 2, /**< Stored settings bypassed after watchdog failure. */
} board_config_source_t;

/** @brief Stable diagnostic reasons for fallback to SDK/board defaults. */
typedef enum {
    BOARD_CONFIG_OK = 0,                  /**< Stored record accepted. */
    BOARD_CONFIG_ERASED = 1,              /**< Entire sector is erased. */
    BOARD_CONFIG_INVALID_HEADER = 2,      /**< Signature, length or reserved fields are invalid. */
    BOARD_CONFIG_CRC_FAILED = 3,          /**< Record checksum mismatch. */
    BOARD_CONFIG_UNSUPPORTED_VERSION = 4, /**< Unknown record version. */
    BOARD_CONFIG_INVALID_PROFILE = 5,     /**< Unknown or target-incompatible clock profile. */
    BOARD_CONFIG_WATCHDOG_RECOVERY = 6,   /**< Previous watchdog timeout bypassed the stored clock. */
    BOARD_CONFIG_CLOCK_UNAVAILABLE = 7,   /**< SDK cannot synthesize the requested frequency. */
} board_config_reason_t;

/** @brief Boot decision retained in RAM for read-only diagnostics. */
typedef struct {
    board_config_source_t source; /**< Stored settings, defaults, or watchdog recovery. */
    board_config_reason_t reason; /**< Validation or recovery outcome. */
    uint32_t stored_profile;      /**< Accepted stored profile, or unavailable sentinel. */
    board_clock_profile_t active_profile; /**< Profile actually selected for this boot. */
} board_config_status_t;

/**
 * @brief Calculate CRC-32/ISO-HDLC, including initial/final XOR.
 * @param data Input bytes.
 * @param size Input size in bytes.
 * @return Standard reflected CRC-32.
 */
uint32_t board_config_crc32(const uint8_t *data, size_t size);

/**
 * @brief Validate an entire sector and select complete defaults on any failure.
 * @param sector Flash sector bytes, or NULL for unavailable storage.
 * @param size Available bytes; must equal BOARD_CONFIG_SECTOR_SIZE.
 * @param rp2350 Whether the target belongs to the RP2350 family.
 * @param recovery Whether an unexpected watchdog reset requires a safe boot.
 * @return Boot decision; never writes storage or changes clocks.
 */
board_config_status_t board_config_decode(const uint8_t *sector, size_t size, bool rp2350, bool recovery);

/**
 * @brief Map a supported profile to its exact clock request.
 * @param profile Permanent profile ID.
 * @return kHz, or zero for the no-override default/unknown profile.
 */
uint32_t board_config_clock_khz(board_clock_profile_t profile);

/**
 * @brief Check a permanent profile ID against the target family.
 * @param profile Untrusted profile ID.
 * @param rp2350 Whether the target belongs to the RP2350 family.
 * @return True for SDK default or the family's supported overclock profile.
 */
bool board_config_profile_supported(uint32_t profile, bool rp2350);

/**
 * @brief Encode a version-1 record; the caller leaves the remaining sector erased.
 * @param record Destination with BOARD_CONFIG_RECORD_SIZE bytes.
 * @param profile Previously validated profile.
 */
void board_config_encode(uint8_t record[BOARD_CONFIG_RECORD_SIZE], board_clock_profile_t profile);

#endif
