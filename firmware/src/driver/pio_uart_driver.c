/**
 * @file pio_uart_driver.c
 * @brief PIO UART backend for PicoUart logical UART ports.
 */

#include "driver/pio_uart_driver.h"

bool pio_uart_driver_init(pio_uart_driver_t *driver)
{
    if (driver == NULL) {
        return false;
    }

    if ((driver->config.tx_pin == PIO_UART_DRIVER_PIN_UNASSIGNED) ||
        (driver->config.rx_pin == PIO_UART_DRIVER_PIN_UNASSIGNED)) {
        return false;
    }

    /* ponytail: the first driver pass only reserves the 4 logical PIO UART slots; real PIO RX/TX programs can be added later without changing the higher-level 6-port topology. */
    return false;
}

void pio_uart_driver_deinit(pio_uart_driver_t *driver)
{
    if (driver == NULL) {
        return;
    }

    driver->initialized = false;
}

size_t pio_uart_driver_read(pio_uart_driver_t *driver, uint8_t *data, size_t capacity)
{
    (void)driver;
    (void)data;
    (void)capacity;

    return 0u;
}

size_t pio_uart_driver_write(pio_uart_driver_t *driver, const uint8_t *data, size_t length)
{
    (void)driver;
    (void)data;
    (void)length;

    return 0u;
}