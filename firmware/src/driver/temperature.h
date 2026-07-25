/**
 * @file temperature.h
 * @brief Internal ADC temperature-sensor helpers for PicoUart firmware.
 */

#ifndef TEMPERATURE_H
#define TEMPERATURE_H

/** @brief Initialize the ADC and enable its internal temperature sensor. */
void temperature_init(void);

/**
 * @brief Read the current RP2 internal temperature sensor estimate.
 * @return Temperature in degrees Celsius.
 */
float temperature_read_celsius(void);

#endif