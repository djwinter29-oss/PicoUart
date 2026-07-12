/**
 * @file hw_uart_driver.c
 * @brief Hardware UART backend for PicoUart logical UART ports.
 */

#include "driver/hw_uart_driver.h"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/structs/dma.h"

/** @brief DMA ring selector bits for the RX buffer size above. */
#define HW_UART_DRIVER_RX_RING_BITS 10u

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

void hw_uart_driver_poll(hw_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->initialized) {
        return;
    }

    hw_uart_driver_poll_tx(driver);
}

bool hw_uart_driver_init(hw_uart_driver_t *driver)
{
    dma_channel_config rx_dma_config;

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
    uart_init(driver->config.instance, driver->config.baud_rate);
    uart_set_hw_flow(driver->config.instance, false, false);
    uart_set_format(
        driver->config.instance,
        driver->config.data_bits,
        driver->config.stop_bits,
        driver->config.parity);
    uart_set_fifo_enabled(driver->config.instance, true);

    rx_dma_config = dma_channel_get_default_config((uint)driver->rx_dma_channel);
    channel_config_set_transfer_data_size(&rx_dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_dma_config, false);
    channel_config_set_write_increment(&rx_dma_config, true);
    channel_config_set_dreq(&rx_dma_config, uart_get_dreq(driver->config.instance, false));
    channel_config_set_ring(&rx_dma_config, true, HW_UART_DRIVER_RX_RING_BITS);
    dma_channel_configure(
        (uint)driver->rx_dma_channel,
        &rx_dma_config,
        driver->rx_storage,
        &uart_get_hw(driver->config.instance)->dr,
        0xffffffffu,
        true);

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
    size_t producer;

    if ((driver == NULL) || (data == NULL) || !driver->initialized) {
        return 0u;
    }

    producer = hw_uart_driver_rx_write_index(driver);
    ring_buffer_publish_producer(&driver->rx_ring, producer, true);
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
    if ((driver == NULL) || !driver->initialized || (baud_rate == 0u)) {
        return false;
    }

    driver->config.baud_rate = baud_rate;
    uart_set_baudrate(driver->config.instance, baud_rate);
    return true;
}

size_t hw_uart_driver_write(hw_uart_driver_t *driver, const uint8_t *data, size_t length)
{
    size_t written;

    if ((driver == NULL) || (data == NULL) || !driver->initialized) {
        return 0u;
    }

    hw_uart_driver_poll_tx(driver);

    written = ring_buffer_write(&driver->tx_ring, data, length);
    if (!driver->tx_active) {
        (void)hw_uart_driver_start_tx_dma(driver);
    }

    return written;
}