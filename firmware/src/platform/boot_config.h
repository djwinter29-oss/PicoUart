/**
 * @file boot_config.h
 * @brief Versioned persistent boot settings and target-checked clock profiles.
 */
#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Diagnostic sentinel when no valid stored profile is available. */
#define BOOT_CONFIG_PROFILE_UNAVAILABLE UINT32_MAX

/** @brief Permanent profile IDs; non-default values always identify one exact frequency. */
typedef enum {
    BOOT_CLOCK_DEFAULT = 0, /**< Leave the SDK-selected startup clock unchanged. */
    BOOT_CLOCK_250_MHZ = 1, /**< Exactly 250 MHz; initially supported on RP2040 only. */
    BOOT_CLOCK_280_MHZ = 2, /**< Exactly 280 MHz; initially supported on RP2350 only. */
} boot_clock_profile_t;

/** @brief Source of the active boot settings. */
typedef enum {
    BOOT_CONFIG_DEFAULTS = 0, /**< Defaults because settings are absent or rejected. */
    BOOT_CONFIG_STORED = 1,   /**< Valid stored settings. */
    BOOT_CONFIG_RECOVERY = 2, /**< Stored settings bypassed after watchdog failure. */
} boot_config_source_t;

/** @brief Stable diagnostic reasons for fallback to SDK defaults. */
typedef enum {
    BOOT_CONFIG_OK = 0,                  /**< Stored record accepted. */
    BOOT_CONFIG_ERASED = 1,              /**< Entire sector is erased. */
    BOOT_CONFIG_INVALID_HEADER = 2,      /**< Signature, length or reserved fields are invalid. */
    BOOT_CONFIG_CRC_FAILED = 3,          /**< Record checksum mismatch. */
    BOOT_CONFIG_UNSUPPORTED_VERSION = 4, /**< Unknown record version. */
    BOOT_CONFIG_INVALID_PROFILE = 5,     /**< Unknown or target-incompatible clock profile. */
    BOOT_CONFIG_WATCHDOG_RECOVERY = 6,   /**< Previous watchdog timeout bypassed the stored clock. */
    BOOT_CONFIG_CLOCK_UNAVAILABLE = 7,   /**< SDK cannot synthesize the requested frequency. */
} boot_config_reason_t;

/** @brief Boot decision retained in RAM for read-only diagnostics. */
typedef struct {
    boot_config_source_t source; /**< Stored settings, defaults, or watchdog recovery. */
    boot_config_reason_t reason; /**< Validation or recovery outcome. */
    uint32_t stored_profile;      /**< Accepted stored profile, or unavailable sentinel. */
    boot_clock_profile_t active_profile; /**< Profile actually selected for this boot. */
} boot_config_status_t;

/**
 * @brief Load and cache the boot decision from persistent storage.
 * @param rp2350 Whether the target belongs to the RP2350 family.
 * @param recovery Whether an unexpected watchdog reset requires a safe boot.
 */
void boot_config_load(bool rp2350, bool recovery);

/**
 * @brief Read the immutable cached boot decision.
 * @return Read-only pointer to the module-owned status snapshot.
 */
const boot_config_status_t *boot_config_get(void);

/**
 * @brief Replace the cached status after the system applies the selected clock.
 * @param status Complete status snapshot to retain.
 */
void boot_config_set(const boot_config_status_t *status);

/**
 * @brief Map a supported profile to its exact clock request.
 * @param profile Permanent profile ID.
 * @return kHz, or zero for the no-override default/unknown profile.
 */
uint32_t boot_config_clock_khz(boot_clock_profile_t profile);

/**
 * @brief Check a permanent profile ID against the target family.
 * @param profile Untrusted profile ID.
 * @param rp2350 Whether the target belongs to the RP2350 family.
 * @return True for SDK default or the family's supported overclock profile.
 */
bool boot_config_profile_supported(uint32_t profile, bool rp2350);

/**
 * @brief Save and verify a supported profile without changing the running clock.
 * @param profile Untrusted permanent profile ID.
 * @param rp2350 Whether the target belongs to the RP2350 family.
 * @return True after full readback validation or when the same valid profile is already stored.
 */
bool boot_config_save(uint32_t profile, bool rp2350);

#endif
