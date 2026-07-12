/**
 * @file hw_uart_driver.c
 * @brief Hardware UART backend for PicoUart logical UART ports.
 */

#include "driver/hw_uart_driver.h"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/structs/dma.h"

#include <string.h>

/** @brief DMA RX ring size in bytes. Must remain a power of two. */
#define HW_UART_DRIVER_RX_BUFFER_SIZE 256u
/** @brief DMA TX bounce-buffer size in bytes. */
#define HW_UART_DRIVER_TX_BUFFER_SIZE 256u
/** @brief DMA ring selector bits for the RX buffer size above. */
#define HW_UART_DRIVER_RX_RING_BITS 8u

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
    uintptr_t write_addr = dma_hw->ch[driver->rx_dma_channel].write_addr;
    uintptr_t base_addr = (uintptr_t)driver->rx_buffer;

    return (size_t)((write_addr - base_addr) & (HW_UART_DRIVER_RX_BUFFER_SIZE - 1u));
}

static void hw_uart_driver_poll_tx(hw_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->tx_active) {
        return;
    }

    if (!dma_channel_is_busy((uint)driver->tx_dma_channel)) {
        driver->tx_active = false;
        driver->tx_length = 0u;
    }
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
    driver->rx_read_index = 0u;
    driver->tx_length = 0u;
    driver->tx_active = false;
    memset(driver->rx_buffer, 0, sizeof(driver->rx_buffer));
    memset(driver->tx_buffer, 0, sizeof(driver->tx_buffer));

    if (!dma_channel_claim_unused(true, (uint *)&driver->rx_dma_channel)) {
        return false;
    }

    if (!dma_channel_claim_unused(true, (uint *)&driver->tx_dma_channel)) {
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
        driver->rx_buffer,
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
    size_t count = 0u;
    size_t write_index;

    if ((driver == NULL) || (data == NULL) || !driver->initialized) {
        return 0u;
    }

    write_index = hw_uart_driver_rx_write_index(driver);

    /* ponytail: this first DMA RX pass uses a ring buffer without explicit overrun tracking; if software falls behind by a full ring, old bytes can be overwritten, which is acceptable for bring-up and can be upgraded later with DMA IRQ watermarking or a larger counted ring. */
    while ((count < capacity) && (driver->rx_read_index != write_index)) {
        data[count] = driver->rx_buffer[driver->rx_read_index];
        driver->rx_read_index = (driver->rx_read_index + 1u) & (HW_UART_DRIVER_RX_BUFFER_SIZE - 1u);
        count += 1u;
    }

    return count;
}

size_t hw_uart_driver_write(hw_uart_driver_t *driver, const uint8_t *data, size_t length)
{
    dma_channel_config tx_dma_config;

    if ((driver == NULL) || (data == NULL) || !driver->initialized) {
        return 0u;
    }

    hw_uart_driver_poll_tx(driver);

    if (driver->tx_active) {
        return 0u;
    }

    if (length > HW_UART_DRIVER_TX_BUFFER_SIZE) {
        length = HW_UART_DRIVER_TX_BUFFER_SIZE;
    }

    if (length == 0u) {
        return 0u;
    }

    memcpy(driver->tx_buffer, data, length);
    driver->tx_length = length;

    tx_dma_config = dma_channel_get_default_config((uint)driver->tx_dma_channel);
    channel_config_set_transfer_data_size(&tx_dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_dma_config, true);
    channel_config_set_write_increment(&tx_dma_config, false);
    channel_config_set_dreq(&tx_dma_config, uart_get_dreq(driver->config.instance, true));
    dma_channel_configure(
        (uint)driver->tx_dma_channel,
        &tx_dma_config,
        &uart_get_hw(driver->config.instance)->dr,
        driver->tx_buffer,
        (uint32_t)length,
        true);

    driver->tx_active = true;

    return length;
}