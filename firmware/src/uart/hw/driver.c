/**
 * @file driver.c
 * @brief Hardware UART backend for PicoUart logical UART ports.
 */

#include "uart/hw/driver.h"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/structs/dma.h"
#include "pico/stdlib.h"

static void hw_uart_driver_configure_uart(hw_uart_driver_t *driver)
{
    uart_init(driver->config.instance, driver->config.baud_rate);
    uart_set_hw_flow(driver->config.instance, false, false);
    uart_set_format(driver->config.instance,
                    driver->config.data_bits,
                    driver->config.stop_bits,
                    driver->config.parity);
    uart_set_fifo_enabled(driver->config.instance, true);
}

static void hw_uart_driver_start_rx_dma(hw_uart_driver_t *driver)
{
    dma_channel_config rx_dma_config;

    rx_dma_config = dma_channel_get_default_config((uint)driver->rx_dma_channel);
    channel_config_set_transfer_data_size(&rx_dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_dma_config, false);
    channel_config_set_write_increment(&rx_dma_config, true);
    channel_config_set_dreq(&rx_dma_config, uart_get_dreq(driver->config.instance, false));
    channel_config_set_ring(&rx_dma_config, true, PICO_UART_HW_UART_RX_DMA_RING_BITS);
    dma_channel_configure((uint)driver->rx_dma_channel,
                          &rx_dma_config,
                          driver->rx_storage,
                          &uart_get_hw(driver->config.instance)->dr,
                          0xffffffffu,
                          true);
}

static bool hw_uart_driver_line_format_supported(uint32_t baud_rate,
                                                 uint8_t data_bits,
                                                 uint8_t stop_bits,
                                                 uart_parity_t parity)
{
    if (baud_rate == 0u) {
        return false;
    }

    if ((data_bits < 5u) || (data_bits > 8u)) {
        return false;
    }

    if ((stop_bits != 1u) && (stop_bits != 2u)) {
        return false;
    }

    return (parity == UART_PARITY_NONE) ||
           (parity == UART_PARITY_ODD) ||
           (parity == UART_PARITY_EVEN);
}

static void hw_uart_driver_release_dma(hw_uart_driver_t *driver)
{
    if (driver->rx_dma_channel >= 0) {
        dma_channel_abort((uint)driver->rx_dma_channel);
        dma_channel_unclaim((uint)driver->rx_dma_channel);
        driver->rx_dma_channel = -1;
    }

    if (driver->tx_dma_channel >= 0) {
        dma_channel_abort((uint)driver->tx_dma_channel);
        dma_channel_unclaim((uint)driver->tx_dma_channel);
        driver->tx_dma_channel = -1;
    }
}

static size_t hw_uart_driver_rx_write_index(const hw_uart_driver_t *driver)
{
    uint32_t remaining = dma_hw->ch[driver->rx_dma_channel].transfer_count;
    return (size_t)(0xffffffffu - remaining);
}

static bool hw_uart_driver_start_tx_dma(hw_uart_driver_t *driver)
{
    dma_channel_config tx_dma_config;
    ring_buffer_span_t span;

    if ((driver == NULL) || driver->tx_active) {
        return false;
    }

    span = ring_buffer_read_span(&driver->tx_ring);
    if (span.length == 0u) {
        return false;
    }

    tx_dma_config = dma_channel_get_default_config((uint)driver->tx_dma_channel);
    channel_config_set_transfer_data_size(&tx_dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_dma_config, true);
    channel_config_set_write_increment(&tx_dma_config, false);
    channel_config_set_dreq(&tx_dma_config, uart_get_dreq(driver->config.instance, true));
    dma_channel_configure(
        (uint)driver->tx_dma_channel,
        &tx_dma_config,
        &uart_get_hw(driver->config.instance)->dr,
        span.data,
        (uint32_t)span.length,
        true);

    driver->tx_dma_bytes_in_flight = span.length;
    driver->tx_active = true;
    return true;
}

static void hw_uart_driver_poll_tx(hw_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->tx_active) {
        return;
    }

    if (!dma_channel_is_busy((uint)driver->tx_dma_channel)) {
        (void)ring_buffer_commit_consumed(&driver->tx_ring, driver->tx_dma_bytes_in_flight);
        driver->tx_active = false;
        driver->tx_dma_bytes_in_flight = 0u;
        (void)hw_uart_driver_start_tx_dma(driver);
    }
}

static void hw_uart_driver_publish_rx(hw_uart_driver_t *driver)
{
    size_t producer;

    if ((driver == NULL) || !driver->initialized) {
        return;
    }

    producer = hw_uart_driver_rx_write_index(driver);
    ring_buffer_publish_producer(&driver->rx_ring, producer, true);
}

void hw_uart_driver_poll(hw_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->initialized) {
        return;
    }

    hw_uart_driver_publish_rx(driver);
    hw_uart_driver_poll_tx(driver);
    if (!driver->tx_active) {
        (void)hw_uart_driver_start_tx_dma(driver);
    }
}

bool hw_uart_driver_init(hw_uart_driver_t *driver)
{
    if ((driver == NULL) || (driver->config.instance == NULL)) {
        return false;
    }

    if ((driver->config.tx_pin == UART_DRIVER_PIN_UNASSIGNED) ||
        (driver->config.rx_pin == UART_DRIVER_PIN_UNASSIGNED)) {
        return false;
    }

    driver->rx_dma_channel = -1;
    driver->tx_dma_channel = -1;
    driver->tx_dma_bytes_in_flight = 0u;
    driver->tx_active = false;

    if (!ring_buffer_init(&driver->rx_ring, driver->rx_storage, sizeof(driver->rx_storage))) {
        return false;
    }

    if (!ring_buffer_init(&driver->tx_ring, driver->tx_storage, sizeof(driver->tx_storage))) {
        return false;
    }

    driver->rx_dma_channel = dma_claim_unused_channel(true);
    if (driver->rx_dma_channel < 0) {
        return false;
    }

    driver->tx_dma_channel = dma_claim_unused_channel(true);
    if (driver->tx_dma_channel < 0) {
        hw_uart_driver_release_dma(driver);
        return false;
    }

    gpio_set_function(driver->config.tx_pin, GPIO_FUNC_UART);
    gpio_set_function(driver->config.rx_pin, GPIO_FUNC_UART);
    hw_uart_driver_configure_uart(driver);
    hw_uart_driver_start_rx_dma(driver);

    driver->initialized = true;
    return true;
}

void hw_uart_driver_deinit(hw_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->initialized) {
        return;
    }

    hw_uart_driver_release_dma(driver);
    uart_deinit(driver->config.instance);
    driver->initialized = false;
}

size_t hw_uart_driver_read(hw_uart_driver_t *driver, uint8_t *data, size_t capacity)
{
    if ((driver == NULL) || (data == NULL) || !driver->initialized) {
        return 0u;
    }

    return ring_buffer_read(&driver->rx_ring, data, capacity);
}

size_t hw_uart_driver_write_available(const hw_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->initialized) {
        return 0u;
    }

    return ring_buffer_free_space(&driver->tx_ring);
}

bool hw_uart_driver_set_baud_rate(hw_uart_driver_t *driver, uint32_t baud_rate)
{
    if ((driver == NULL) || !driver->initialized) {
        return false;
    }

    return hw_uart_driver_set_line_format(driver,
                                          baud_rate,
                                          driver->config.data_bits,
                                          driver->config.stop_bits,
                                          driver->config.parity);
}

bool hw_uart_driver_set_line_format(hw_uart_driver_t *driver,
                                    uint32_t baud_rate,
                                    uint8_t data_bits,
                                    uint8_t stop_bits,
                                    uart_parity_t parity)
{
    if ((driver == NULL) || !driver->initialized ||
        !hw_uart_driver_line_format_supported(baud_rate, data_bits, stop_bits, parity)) {
        return false;
    }

    if ((ring_buffer_occupancy(&driver->tx_ring) != 0u) || driver->tx_active) {
        return false;
    }

    uart_tx_wait_blocking(driver->config.instance);
    hw_uart_driver_publish_rx(driver);
    dma_channel_abort((uint)driver->rx_dma_channel);
    dma_channel_abort((uint)driver->tx_dma_channel);
    driver->tx_dma_bytes_in_flight = 0u;
    driver->tx_active = false;
    uart_deinit(driver->config.instance);

    driver->config.baud_rate = baud_rate;
    driver->config.data_bits = data_bits;
    driver->config.stop_bits = stop_bits;
    driver->config.parity = parity;

    hw_uart_driver_configure_uart(driver);
    uart_get_hw(driver->config.instance)->rsr = 0u;
    hw_uart_driver_start_rx_dma(driver);
    return true;
}

size_t hw_uart_driver_write(hw_uart_driver_t *driver, const uint8_t *data, size_t length)
{
    size_t written;

    if ((driver == NULL) || (data == NULL) || !driver->initialized) {
        return 0u;
    }

    written = ring_buffer_write(&driver->tx_ring, data, length);

    return written;
}