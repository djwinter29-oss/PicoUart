/**
 * @file temperature.c
 * @brief Internal ADC temperature-sensor helpers for PicoUart firmware.
 */

#include "driver/temperature.h"

#include "hardware/adc.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief RP2 ADC input index for the integrated temperature sensor. */
#define TEMPERATURE_ADC_CHANNEL 4u
/** @brief RP2 ADC reference voltage assumed by the SDK temperature conversion. */
#define TEMPERATURE_ADC_REFERENCE_VOLTAGE 3.3f
/** @brief RP2 ADC conversion resolution in bits. */
#define TEMPERATURE_ADC_RESOLUTION_BITS 12u
/** @brief RP2 internal sensor voltage at 27 degrees Celsius. */
#define TEMPERATURE_SENSOR_VOLTAGE_AT_27C 0.706f
/** @brief RP2 internal sensor voltage decrease per degree Celsius. */
#define TEMPERATURE_SENSOR_VOLTAGE_PER_CELSIUS 0.001721f

/** @copydoc temperature_init */
void temperature_init(void)
{
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(TEMPERATURE_ADC_CHANNEL);
}

/** @copydoc temperature_read_celsius */
float temperature_read_celsius(void)
{
    adc_select_input(TEMPERATURE_ADC_CHANNEL);
    uint16_t raw = adc_read();
    float voltage = (float)raw * TEMPERATURE_ADC_REFERENCE_VOLTAGE /
                    (float)(1u << TEMPERATURE_ADC_RESOLUTION_BITS);

    return 27.0f - ((voltage - TEMPERATURE_SENSOR_VOLTAGE_AT_27C) /
                     TEMPERATURE_SENSOR_VOLTAGE_PER_CELSIUS);
}