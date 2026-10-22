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

/** @brief Predefined clock profiles ordered by frequency and shared by settings, storage, and HID. */
typedef enum {
    BOOT_CLOCK_DEFAULT = 0, /**< Leave the SDK-selected startup clock unchanged. */
    BOOT_CLOCK_150_MHZ = 1, /**< Exactly 150 MHz on RP2040 or RP2350. */
    BOOT_CLOCK_200_MHZ = 2, /**< Exactly 200 MHz on RP2040 or RP2350. */
    BOOT_CLOCK_250_MHZ = 3, /**< Exactly 250 MHz on RP2040 or RP2350. */
    BOOT_CLOCK_280_MHZ = 4, /**< Exactly 280 MHz on RP2350 only. */
} boot_clock_profile_t;

/** @brief Requested settings, independent of storage format and boot diagnostics. */
typedef struct {
    boot_clock_profile_t clock_profile; /**< Requested predefined clock; default leaves the SDK clock unchanged. */
} boot_config_t;

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

/** @brief Boot-only diagnostics; separate from staged settings and unchanged by set/save. */
typedef struct {
    boot_config_source_t source; /**< Stored settings, defaults, or watchdog recovery. */
    boot_config_reason_t reason; /**< Validation or recovery outcome. */
    uint32_t stored_profile;      /**< Accepted stored profile, or unavailable sentinel. */
    boot_clock_profile_t active_profile; /**< Applied boot override; default means the SDK-selected clock. */
} boot_config_status_t;

/** @brief Detailed outcome of a persistent configuration save. */
typedef enum {
    BOOT_CONFIG_SAVE_OK = 0, /**< Existing or newly written record passed validation. */
    BOOT_CONFIG_SAVE_FLASH_FAILED, /**< SDK flash-safe erase/program operation failed. */
    BOOT_CONFIG_SAVE_VERIFY_FAILED, /**< Programmed bytes or decoded record failed verification. */
} boot_config_save_result_t;

/**
 * @brief Load and cache the boot decision from persistent storage.
 * @param recovery Whether an unexpected watchdog reset requires a safe boot.
 * @note Invalid storage selects defaults in RAM without rewriting flash. Call only during startup.
 */
void boot_config_load(bool recovery);

/**
 * @brief Read the cached requested settings.
 * @return Read-only pointer to module-owned settings, updated by load or set.
 */
const boot_config_t *boot_config_get(void);

/**
 * @brief Stage target-supported settings without changing clocks, diagnostics, or storage.
 * @param config Requested settings; null or unsupported values leave the cache unchanged.
 * @return True when the settings were accepted.
 */
bool boot_config_set(const boot_config_t *config);

/** @brief Read the boot-only diagnostic snapshot; set/save do not change it. @return Read-only status. */
const boot_config_status_t *boot_config_get_status(void);

/** @brief Record a boot clock-application failure before UART/USB startup. */
void boot_config_clock_unavailable(void);

/**
 * @brief Map a supported profile to its exact clock request.
 * @param profile Predefined profile ID.
 * @return kHz, or zero for the no-override default/unknown profile.
 */
uint32_t boot_config_clock_khz(boot_clock_profile_t profile);

/**
 * @brief Check a predefined profile ID against the target family.
 * @param profile Untrusted profile ID.
 * @return True for SDK default or the family's supported overclock profile.
 */
bool boot_config_profile_supported(uint32_t profile);

/**
 * @brief Save and verify staged settings without changing clocks or the boot diagnostic snapshot.
 * @return True after full readback validation or when the same valid profile is already stored.
 * @note Call on core 0 with UART traffic stopped and the core-1 flash lockout victim initialized.
 */
bool boot_config_save(void);

/**
 * @brief Save settings and distinguish flash-operation failure from readback verification failure.
 * @return Detailed save outcome for HID command reporting.
 */
boot_config_save_result_t boot_config_save_with_result(void);

#endif
