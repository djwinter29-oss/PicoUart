/**
 * @file temperature.h
 * @brief Internal ADC temperature-sensor helpers for PicoUart firmware.
 */

#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <math.h>
#include <stdint.h>

/** @brief Lowest temperature representable by the HID centidegree field. */
#define TEMPERATURE_HID_MIN_CENTIDEGREES INT16_MIN
/** @brief Highest temperature representable by the HID centidegree field. */
#define TEMPERATURE_HID_MAX_CENTIDEGREES INT16_MAX

/**
 * @brief Initialize the ADC and enable its internal temperature sensor.
 *
 * The ADC remains initialized for the lifetime of the firmware because HID
 * board-status reads may sample the temperature at any time.
 */
void temperature_init(void);

/**
 * @brief Read the current RP2 internal temperature sensor estimate.
 *
 * ADC access is owned by the main USB core; callers must not invoke this from
 * another core or interrupt context without adding ADC synchronization.
 * @return Temperature in degrees Celsius.
 */
float temperature_read_celsius(void);

/**
 * @brief Convert a Celsius reading to the bounded HID centidegree format.
 * @param temperature_celsius Temperature estimate in degrees Celsius.
 * @return Truncated centidegrees, saturated to the signed 16-bit HID range;
 *         non-finite input returns zero.
 */
static inline int16_t temperature_to_hid_centidegrees(float temperature_celsius)
{
	if (!isfinite(temperature_celsius)) {
		return 0;
	}

	float centidegrees = temperature_celsius * 100.0f;
	if (centidegrees <= (float)TEMPERATURE_HID_MIN_CENTIDEGREES) {
		return TEMPERATURE_HID_MIN_CENTIDEGREES;
	}
	if (centidegrees >= (float)TEMPERATURE_HID_MAX_CENTIDEGREES) {
		return TEMPERATURE_HID_MAX_CENTIDEGREES;
	}

	return (int16_t)centidegrees;
}

#endif