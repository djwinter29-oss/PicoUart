/**
 * @file test_pio_tx_driver.c
 * @brief Execute real PIO TX paths with register-call mocks and real ring ownership.
 */

#include "unity.h"
#include <string.h>

#ifndef PIO_UART_DRIVER_SOURCE
#define PIO_UART_DRIVER_SOURCE "../src/uart/pio/pio_uart_driver.c"
#endif
#include PIO_UART_DRIVER_SOURCE

pio_hw_t pio_tx_test_pio0;
pio_hw_t pio_tx_test_pio1;

static pio_uart_driver_t driver;
static const uart_driver_port_info_t port = {
    .id = UART_PORT_2, .backend = UART_DRIVER_BACKEND_PIO, .baud_rate = 115200u,
    .tx_pin = 8u, .rx_pin = 9u, .data_bits = 8u, .stop_bits = 1u, .parity = UART_DRIVER_PARITY_NONE,
};
static const pio_uart_driver_config_t config = {
    .port = &port, .pio = pio0, .tx_state_machine = 0u, .rx_state_machine = 1u,
    .tx_dma_start_threshold = 64u,
};

static uint fifo_level;
static uint fifo_writes;
static uint configure_calls;
static uint read_address_writes;
static uint launches;
static uint service_notes;
static bool dma_busy;
static dma_channel_config configured;
static const volatile uint8_t *dma_source;
static volatile void *dma_destination;
static uint32_t dma_count;
static uint32_t launch_consumer;
static uint8_t fifo_bytes[8];

void service_load_note(unsigned int core)
{
    TEST_ASSERT_EQUAL_UINT(1u, core);
    ++service_notes;
}

uint pio_get_dreq(PIO selected, uint sm, bool tx)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_EQUAL_UINT(config.tx_state_machine, sm);
    TEST_ASSERT_TRUE(tx);
    return 9u;
}

uint pio_sm_get_tx_fifo_level(PIO selected, uint sm)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_EQUAL_UINT(config.tx_state_machine, sm);
    return fifo_level;
}

bool pio_sm_is_tx_fifo_empty(PIO selected, uint sm)
{
    return pio_sm_get_tx_fifo_level(selected, sm) == 0u;
}

void pio_sm_put(PIO selected, uint sm, uint32_t value)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_EQUAL_UINT(config.tx_state_machine, sm);
    TEST_ASSERT_FALSE(driver.txstall_waiting);
    TEST_ASSERT_FALSE(dma_busy);
    TEST_ASSERT_LESS_THAN_UINT(8u, fifo_level);
    fifo_bytes[fifo_writes++] = (uint8_t)value;
    ++fifo_level;
}

dma_channel_config dma_channel_get_default_config(uint channel)
{
    TEST_ASSERT_EQUAL_UINT(3u, channel);
    return (dma_channel_config){.enabled = true};
}

void channel_config_set_transfer_data_size(dma_channel_config *settings, uint size)
{
    settings->data_size = size;
}

void channel_config_set_read_increment(dma_channel_config *settings, bool increment)
{
    settings->read_increment = increment;
}

void channel_config_set_write_increment(dma_channel_config *settings, bool increment)
{
    settings->write_increment = increment;
}

void channel_config_set_dreq(dma_channel_config *settings, uint dreq)
{
    settings->dreq = dreq;
}

void dma_channel_configure(uint channel, const dma_channel_config *settings, volatile void *write_address,
                           const volatile void *read_address, uint32_t count, bool trigger)
{
    TEST_ASSERT_EQUAL_UINT(3u, channel);
    TEST_ASSERT_FALSE(trigger);
    TEST_ASSERT_NULL((const void *)read_address);
    TEST_ASSERT_EQUAL_UINT32(0u, count);
    configured = *settings;
    dma_destination = write_address;
    ++configure_calls;
}

void dma_channel_set_read_addr(uint channel, const volatile void *address, bool trigger)
{
    TEST_ASSERT_EQUAL_UINT(3u, channel);
    TEST_ASSERT_FALSE(trigger);
    TEST_ASSERT_FALSE(dma_busy);
    TEST_ASSERT_FALSE(driver.txstall_waiting);
    dma_source = address;
    ++read_address_writes;
}

void dma_channel_set_trans_count(uint channel, uint32_t count, bool trigger)
{
    TEST_ASSERT_EQUAL_UINT(3u, channel);
    TEST_ASSERT_TRUE(trigger);
    TEST_ASSERT_TRUE(configured.enabled);
    TEST_ASSERT_EQUAL_UINT(launches + 1u, read_address_writes);
    TEST_ASSERT_TRUE(driver.tx_dma_active);
    TEST_ASSERT_FALSE(driver.txstall_waiting);
    TEST_ASSERT_EQUAL_UINT(count, driver.tx_dma_bytes_in_flight);
    TEST_ASSERT_EQUAL_PTR(&driver.tx_storage[driver.tx_ring.consumer & driver.tx_ring.mask],
                          (const void *)dma_source);
    launch_consumer = driver.tx_ring.consumer;
    dma_count = count;
    dma_busy = true;
    ++launches;
}

bool dma_channel_is_busy(uint channel)
{
    TEST_ASSERT_EQUAL_UINT(3u, channel);
    return dma_busy;
}

/** @brief Queue real ring data with a recognizable byte sequence. */
static void queue_bytes(size_t count)
{
    uint8_t bytes[768];
    TEST_ASSERT_TRUE(count <= sizeof(bytes));
    for (size_t index = 0u; index < count; ++index) {
        bytes[index] = (uint8_t)index;
    }
    TEST_ASSERT_EQUAL_UINT(count, ring_buffer_write(&driver.tx_ring, bytes, count));
}

/** @brief Model a stale TXSTALL observation and hardware W1C before a fresh reassertion. */
static void assert_fresh_idle_is_required(void)
{
    const uint32_t mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + config.tx_state_machine);
    pio_tx_test_pio0.fdebug = mask;
    TEST_ASSERT_FALSE(pio_uart_driver_tx_shifter_idle(&driver));
    TEST_ASSERT_TRUE(driver.txstall_waiting);
    TEST_ASSERT_EQUAL_UINT32(mask, pio_tx_test_pio0.fdebug);
    pio_tx_test_pio0.fdebug = 0u;
    TEST_ASSERT_FALSE(pio_uart_driver_tx_shifter_idle(&driver));
    pio_tx_test_pio0.fdebug = mask;
    TEST_ASSERT_TRUE(pio_uart_driver_tx_shifter_idle(&driver));
}

void setUp(void)
{
    memset(&driver, 0, sizeof(driver));
    memset(&pio_tx_test_pio0, 0, sizeof(pio_tx_test_pio0));
    driver.config = &config;
    driver.baud_rate = 115200u;
    driver.initialized = true;
    driver.tx_dma_channel = 3;
    TEST_ASSERT_TRUE(ring_buffer_init(&driver.tx_ring, driver.tx_storage, sizeof(driver.tx_storage)));
    fifo_level = 0u;
    fifo_writes = 0u;
    configure_calls = 0u;
    read_address_writes = 0u;
    launches = 0u;
    service_notes = 0u;
    dma_busy = false;
    configured = (dma_channel_config){0};
    dma_source = NULL;
    dma_destination = NULL;
    dma_count = 0u;
    launch_consumer = 0u;
}

void tearDown(void)
{
}

void test_fifo_launch_invalidates_probe_before_writing_bytes(void)
{
    queue_bytes(5u);
    driver.txstall_waiting = true;
    pio_uart_driver_service_tx(&driver);
    TEST_ASSERT_FALSE(driver.txstall_waiting);
    TEST_ASSERT_EQUAL_UINT(5u, fifo_writes);
    TEST_ASSERT_EQUAL_UINT32(5u, driver.tx_ring.consumer);
    TEST_ASSERT_EQUAL_UINT(5u, driver.tx_polled_bytes);
    TEST_ASSERT_EQUAL_UINT(0u, launches);
    for (uint index = 0u; index < fifo_writes; ++index) {
        TEST_ASSERT_EQUAL_UINT8(index, fifo_bytes[index]);
    }
    fifo_level = 0u;
    assert_fresh_idle_is_required();
}

void test_empty_and_full_fifo_paths_do_not_invalidate_probe(void)
{
    driver.txstall_waiting = true;
    pio_uart_driver_drain_tx_fifo(&driver);
    TEST_ASSERT_TRUE(driver.txstall_waiting);
    queue_bytes(5u);
    fifo_level = 8u;
    pio_uart_driver_service_tx(&driver);
    TEST_ASSERT_TRUE(driver.txstall_waiting);
    TEST_ASSERT_EQUAL_UINT(0u, fifo_writes);
    TEST_ASSERT_EQUAL_UINT32(0u, driver.tx_ring.consumer);
}

void test_dma_completion_relaunch_preserves_order_and_requires_fresh_idle(void)
{
    queue_bytes(600u);
    pio_uart_driver_configure_tx_dma(&driver);
    TEST_ASSERT_EQUAL_UINT(1u, configure_calls);
    TEST_ASSERT_EQUAL_PTR(&config.pio->txf[config.tx_state_machine], (const void *)dma_destination);
    TEST_ASSERT_TRUE(configured.enabled);
    TEST_ASSERT_TRUE(configured.read_increment);
    TEST_ASSERT_FALSE(configured.write_increment);
    TEST_ASSERT_EQUAL_UINT(DMA_SIZE_8, configured.data_size);
    TEST_ASSERT_EQUAL_UINT(9u, configured.dreq);
    driver.txstall_waiting = true;
    pio_uart_driver_service_tx(&driver);
    TEST_ASSERT_EQUAL_UINT(1u, launches);
    TEST_ASSERT_FALSE(driver.txstall_waiting);
    TEST_ASSERT_EQUAL_UINT32(256u, dma_count);
    TEST_ASSERT_EQUAL_UINT32(0u, launch_consumer);
    TEST_ASSERT_EQUAL_UINT32(0u, driver.tx_ring.consumer);
    pio_uart_driver_service_tx(&driver);
    TEST_ASSERT_EQUAL_UINT(1u, launches);
    TEST_ASSERT_TRUE(driver.tx_dma_active);
    TEST_ASSERT_EQUAL_UINT32(0u, driver.tx_ring.consumer);
    dma_busy = false;
    pio_uart_driver_poll_tx_dma(&driver);
    TEST_ASSERT_FALSE(driver.tx_dma_active);
    TEST_ASSERT_EQUAL_UINT32(256u, driver.tx_ring.consumer);
    TEST_ASSERT_EQUAL_UINT(256u, driver.tx_dma_bytes);
    pio_uart_driver_poll_tx_dma(&driver);
    TEST_ASSERT_EQUAL_UINT(256u, driver.tx_dma_bytes);
    driver.txstall_waiting = true;
    pio_uart_driver_service_tx(&driver);
    TEST_ASSERT_EQUAL_UINT(2u, launches);
    TEST_ASSERT_FALSE(driver.txstall_waiting);
    TEST_ASSERT_EQUAL_UINT32(256u, launch_consumer);
    TEST_ASSERT_EQUAL_UINT32(256u, driver.tx_ring.consumer);
    TEST_ASSERT_EQUAL_UINT(1u, configure_calls);
    dma_busy = false;
    pio_uart_driver_poll_tx_dma(&driver);
    TEST_ASSERT_EQUAL_UINT32(512u, driver.tx_ring.consumer);
    TEST_ASSERT_EQUAL_UINT(512u, driver.tx_dma_bytes);
    assert_fresh_idle_is_required();
}

void test_dma_relaunch_uses_new_span_after_ring_wrap(void)
{
    driver.tx_ring.producer = driver.tx_ring.consumer = driver.tx_ring.size - 6u;
    queue_bytes(64u);
    pio_uart_driver_configure_tx_dma(&driver);
    driver.txstall_waiting = true;
    TEST_ASSERT_TRUE(pio_uart_driver_start_tx_dma(&driver, 256u));
    TEST_ASSERT_EQUAL_UINT32(6u, dma_count);
    TEST_ASSERT_FALSE(pio_uart_driver_start_tx_dma(&driver, 256u));
    dma_busy = false;
    pio_uart_driver_poll_tx_dma(&driver);
    driver.txstall_waiting = true;
    TEST_ASSERT_TRUE(pio_uart_driver_start_tx_dma(&driver, 256u));
    TEST_ASSERT_FALSE(driver.txstall_waiting);
    TEST_ASSERT_EQUAL_PTR(driver.tx_storage, (const void *)dma_source);
    TEST_ASSERT_EQUAL_UINT32(58u, dma_count);
    TEST_ASSERT_EQUAL_UINT32(driver.tx_ring.size, launch_consumer);
    TEST_ASSERT_EQUAL_UINT(1u, configure_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fifo_launch_invalidates_probe_before_writing_bytes);
    RUN_TEST(test_empty_and_full_fifo_paths_do_not_invalidate_probe);
    RUN_TEST(test_dma_completion_relaunch_preserves_order_and_requires_fresh_idle);
    RUN_TEST(test_dma_relaunch_uses_new_span_after_ring_wrap);
    return UNITY_END();
}