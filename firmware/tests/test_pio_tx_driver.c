/**
 * @file test_pio_tx_driver.c
 * @brief Execute real PIO TX and RX/baud-change paths with SDK mocks and real ring ownership.
 */

#include "unity.h"
#include <string.h>

#ifndef PIO_UART_DRIVER_SOURCE
#define PIO_UART_DRIVER_SOURCE "../src/uart/pio/pio_uart_driver.c"
#endif
#include PIO_UART_DRIVER_SOURCE

pio_hw_t pio_tx_test_pio0;
pio_hw_t pio_tx_test_pio1;
static pio_test_dma_hw_t dma_registers;
pio_test_dma_hw_t *dma_hw = &dma_registers;

static pio_uart_driver_t driver;
static const uart_driver_port_info_t port = {
    .id = UART_PORT_2, .backend = UART_DRIVER_BACKEND_PIO, .baud_rate = 115200u,
    .tx_pin = 8u, .rx_pin = 9u, .data_bits = 8u, .stop_bits = 1u, .parity = UART_DRIVER_PARITY_NONE,
};
static const pio_uart_driver_config_t config = {
    .port = &port, .pio = pio0, .tx_state_machine = 0u, .rx_state_machine = 1u,
    .tx_dma_start_threshold = 64u,
    .pin_flags = PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
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
static uint32_t rx_progress;
static uint32_t expected_rx_producer;
static uint rx_configure_calls;
static uint rx_abort_calls;
static uint rx_ack_calls;
static uint pause_calls;
static uint divider_calls;
static uint divider_restart_calls;
static uint clear_calls[4];
static uint restart_calls[4];
static float dividers[4];
static bool sm_enabled[4];
static bool interrupts_enabled;
static bool rx_irq_enabled;
static bool rx_dma_busy;
static bool rx_fifo_empty;
static bool rx_high;
static bool progress_settles;
static bool parked_tx_high;
static bool parked_tx_output;
static uint parked_tx_function;
static uint claimed_state_machines;
static uint claimed_dma_channels;
static uint rx_pc;
/** @brief Hardware activity injected after RX is paused for the critical recheck. */
typedef enum {
    PIO_TEST_LATE_NONE = 0, /**< No activity after the initial idle checks. */
    PIO_TEST_LATE_RX_FIFO,  /**< RX FIFO receives a word before the critical recheck. */
    PIO_TEST_LATE_RX_PC,    /**< RX state machine leaves its next-start wait. */
    PIO_TEST_LATE_RX_LOW,   /**< RX pin falls while the state machine is being paused. */
    PIO_TEST_LATE_TX_FIFO,  /**< TX FIFO is unexpectedly nonempty at the critical recheck. */
} pio_test_late_condition_t;

static pio_test_late_condition_t late_rx_condition;

void service_load_note(unsigned int core)
{
    TEST_ASSERT_EQUAL_UINT(1u, core);
    ++service_notes;
}

bool pio_sm_is_claimed(PIO selected, uint sm)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_TRUE(sm == config.tx_state_machine || sm == config.rx_state_machine);
    return (claimed_state_machines & (1u << sm)) != 0u;
}

void pio_sm_claim(PIO selected, uint sm)
{
    TEST_ASSERT_FALSE(pio_sm_is_claimed(selected, sm));
    claimed_state_machines |= 1u << sm;
}

void pio_sm_unclaim(PIO selected, uint sm)
{
    TEST_ASSERT_TRUE(pio_sm_is_claimed(selected, sm));
    claimed_state_machines &= ~(1u << sm);
}

int dma_claim_unused_channel(bool required)
{
    TEST_ASSERT_FALSE(required);
    for (uint channel = 2u; channel < 4u; ++channel) {
        if ((claimed_dma_channels & (1u << channel)) == 0u) {
            claimed_dma_channels |= 1u << channel;
            return (int)channel;
        }
    }
    return -1;
}

void dma_channel_unclaim(uint channel)
{
    TEST_ASSERT_TRUE(channel == 2u || channel == 3u);
    TEST_ASSERT_BITS_HIGH(1u << channel, claimed_dma_channels);
    claimed_dma_channels &= ~(1u << channel);
}

uint pio_get_dreq(PIO selected, uint sm, bool tx)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_EQUAL_UINT(tx ? config.tx_state_machine : config.rx_state_machine, sm);
    return tx ? 9u : 8u;
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
    TEST_ASSERT_TRUE(channel == 2u || channel == 3u);
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
    if (channel == 2u) {
        TEST_ASSERT_TRUE(trigger);
        TEST_ASSERT_TRUE(settings->enabled);
        TEST_ASSERT_FALSE(settings->read_increment);
        TEST_ASSERT_TRUE(settings->write_increment);
        TEST_ASSERT_EQUAL_UINT(DMA_SIZE_8, settings->data_size);
        TEST_ASSERT_EQUAL_UINT(8u, settings->dreq);
        TEST_ASSERT_EQUAL_UINT32(uart_dma_rx_transfer_count_encoded(), count);
        TEST_ASSERT_EQUAL_PTR(&driver.rx_storage[driver.rx_ring.producer & driver.rx_ring.mask], write_address);
        TEST_ASSERT_EQUAL_PTR((const void *)((uintptr_t)&config.pio->rxf[config.rx_state_machine] + 3u),
                             (const void *)read_address);
        TEST_ASSERT_TRUE(sm_enabled[config.tx_state_machine]);
        TEST_ASSERT_TRUE(sm_enabled[config.rx_state_machine]);
        TEST_ASSERT_EQUAL_UINT32(0u, driver.rx_dma_last_progress);
        dma_hw->ch[channel].al1_ctrl = DMA_CH0_CTRL_TRIG_EN_BITS;
        dma_hw->ch[channel].transfer_count = count;
        rx_progress = 0u;
        rx_dma_busy = true;
        ++rx_configure_calls;
        return;
    }

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
    if (channel == 2u) {
        TEST_ASSERT_TRUE(trigger);
        TEST_ASSERT_EQUAL_UINT32(uart_dma_rx_transfer_count_encoded(), count);
        dma_hw->ch[channel].transfer_count = count;
        rx_dma_busy = true;
        return;
    }
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
    if (channel == 2u) {
        return rx_dma_busy;
    }
    TEST_ASSERT_EQUAL_UINT(3u, channel);
    return dma_busy;
}

uint32_t clock_get_hz(uint clock)
{
    TEST_ASSERT_EQUAL_UINT(clk_sys, clock);
    return 125000000u;
}

void channel_config_set_ring(dma_channel_config *settings, bool write, uint bits)
{
    (void)settings;
    TEST_ASSERT_TRUE(write);
    TEST_ASSERT_EQUAL_UINT(PICO_UART_PIO_UART_RX_DMA_RING_BITS, bits);
}

void channel_config_set_irq_quiet(dma_channel_config *settings, bool quiet)
{
    (void)settings;
    TEST_ASSERT_FALSE(quiet);
}

void hw_clear_bits(volatile uint32_t *address, uint32_t mask) { *address &= ~mask; }
void hw_set_bits(volatile uint32_t *address, uint32_t mask) { *address |= mask; }

uint32_t save_and_disable_interrupts(void)
{
    uint32_t previous = interrupts_enabled;
    interrupts_enabled = false;
    return previous;
}

void restore_interrupts(uint32_t previous) { interrupts_enabled = previous != 0u; }

void dma_irqn_set_channel_enabled(uint irq, uint channel, bool enabled)
{
    TEST_ASSERT_EQUAL_UINT(1u, irq);
    TEST_ASSERT_EQUAL_UINT(2u, channel);
    rx_irq_enabled = enabled;
}

void dma_irqn_acknowledge_channel(uint irq, uint channel)
{
    TEST_ASSERT_EQUAL_UINT(1u, irq);
    TEST_ASSERT_EQUAL_UINT(2u, channel);
    ++rx_ack_calls;
}

uint32_t uart_dma_rx_progress(uint channel) { TEST_ASSERT_EQUAL_UINT(2u, channel); return rx_progress; }
uint32_t uart_dma_rx_transfer_count_max(void) { return UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350; }
uint32_t uart_dma_rx_transfer_count_encoded(void) { return UART_DMA_RX_TRANSFER_COUNT_MASK_RP2350; }
uint32_t uart_dma_rx_transfer_count_remaining(uint channel)
{
    TEST_ASSERT_EQUAL_UINT(2u, channel);
    return dma_hw->ch[channel].transfer_count;
}

bool uart_dma_rx_wait_paused_progress_stable(uint channel)
{
    TEST_ASSERT_EQUAL_UINT(2u, channel);
    TEST_ASSERT_TRUE(interrupts_enabled);
    TEST_ASSERT_FALSE(rx_irq_enabled);
    TEST_ASSERT_FALSE((dma_hw->ch[channel].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
    TEST_ASSERT_TRUE(sm_enabled[config.rx_state_machine]);
    ++pause_calls;
    rx_progress += 4u;
    return progress_settles;
}

void dma_channel_abort(uint channel)
{
    TEST_ASSERT_EQUAL_UINT(2u, channel);
    TEST_ASSERT_FALSE(interrupts_enabled);
    TEST_ASSERT_FALSE(rx_irq_enabled);
    TEST_ASSERT_FALSE((dma_hw->ch[channel].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
    TEST_ASSERT_EQUAL_UINT32(expected_rx_producer, driver.rx_ring.producer);
    TEST_ASSERT_EQUAL_UINT32(rx_progress, driver.rx_dma_last_progress);
    TEST_ASSERT_TRUE(sm_enabled[config.rx_state_machine]);
    rx_progress = 0xdeadbeefu;
    rx_dma_busy = false;
    ++rx_abort_calls;
}

bool pio_sm_is_rx_fifo_empty(PIO selected, uint sm)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_EQUAL_UINT(config.rx_state_machine, sm);
    return rx_fifo_empty;
}

uint pio_sm_get_pc(PIO selected, uint sm)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_EQUAL_UINT(config.rx_state_machine, sm);
    return rx_pc;
}

bool gpio_get(uint pin) { TEST_ASSERT_EQUAL_UINT(port.rx_pin, pin); return rx_high; }

void gpio_put(uint pin, bool value)
{
    TEST_ASSERT_EQUAL_UINT(port.tx_pin, pin);
    parked_tx_high = value;
}

void gpio_set_dir(uint pin, bool output)
{
    TEST_ASSERT_EQUAL_UINT(port.tx_pin, pin);
    parked_tx_output = output;
}

void gpio_set_function(uint pin, uint function)
{
    TEST_ASSERT_EQUAL_UINT(port.tx_pin, pin);
    parked_tx_function = function;
}

void gpio_deinit(uint pin)
{
    TEST_ASSERT_EQUAL_UINT(port.rx_pin, pin);
}

void pio_set_input_sync_bypass_with_mask(PIO selected, uint32_t value, uint32_t mask)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_EQUAL_UINT32(0u, value);
    TEST_ASSERT_EQUAL_UINT32(1u << port.rx_pin, mask);
}

bool pio_interrupt_get(PIO selected, uint irq)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_EQUAL_UINT(config.rx_state_machine, irq);
    return false;
}

void pio_interrupt_clear(PIO selected, uint irq)
{
    (void)selected;
    (void)irq;
    TEST_FAIL_MESSAGE("No framing interrupt was raised in this fixture");
}

void pio_sm_set_enabled(PIO selected, uint sm, bool enabled)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_TRUE(sm == config.tx_state_machine || sm == config.rx_state_machine);
    sm_enabled[sm] = enabled;
    if (!enabled && sm == config.rx_state_machine) {
        TEST_ASSERT_FALSE(interrupts_enabled);
        if (late_rx_condition == PIO_TEST_LATE_RX_FIFO) rx_fifo_empty = false;
        if (late_rx_condition == PIO_TEST_LATE_RX_PC) ++rx_pc;
        if (late_rx_condition == PIO_TEST_LATE_RX_LOW) rx_high = false;
        if (late_rx_condition == PIO_TEST_LATE_TX_FIFO) fifo_level = 1u;
    }
}

void pio_sm_set_clkdiv(PIO selected, uint sm, float divider)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_FALSE(sm_enabled[config.tx_state_machine]);
    TEST_ASSERT_FALSE(sm_enabled[config.rx_state_machine]);
    TEST_ASSERT_TRUE(interrupts_enabled);
    TEST_ASSERT_EQUAL_UINT(1u, rx_abort_calls);
    TEST_ASSERT_EQUAL_UINT(0u, rx_configure_calls);
    dividers[sm] = divider;
    ++divider_calls;
}

void pio_clkdiv_restart_sm_mask(PIO selected, uint mask)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_EQUAL_UINT((1u << config.tx_state_machine) | (1u << config.rx_state_machine), mask);
    TEST_ASSERT_EQUAL_UINT(2u, divider_calls);
    ++divider_restart_calls;
}

void pio_sm_clear_fifos(PIO selected, uint sm)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_FALSE(sm_enabled[sm]);
    ++clear_calls[sm];
}

void pio_sm_restart(PIO selected, uint sm)
{
    TEST_ASSERT_EQUAL_PTR(config.pio, selected);
    TEST_ASSERT_FALSE(sm_enabled[sm]);
    TEST_ASSERT_EQUAL_UINT(1u, clear_calls[sm]);
    ++restart_calls[sm];
}

/** @brief Seed unread RX across ring wrap, including four beats retiring during the pause. */
static void seed_baud_change(void)
{
    driver.rx_ring.producer = driver.rx_ring.size - 2u;
    driver.rx_ring.consumer = driver.rx_ring.producer - 12u;
    for (uint32_t index = 0u; index < 20u; ++index) {
        driver.rx_storage[(driver.rx_ring.consumer + index) & driver.rx_ring.mask] = (uint8_t)(0xa0u + index);
    }
    expected_rx_producer = driver.rx_ring.producer + 8u;
    driver.rx_dma_last_progress = 20u;
    driver.uart_rx_bytes = 100u;
    rx_progress = 24u;
    driver.txstall_waiting = true;
    config.pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + config.tx_state_machine);
}

/** @brief Check final RX publication, preserved bytes, and resumed DMA/state-machine ownership. */
static void assert_rx_preserved_and_resumed(void)
{
    TEST_ASSERT_EQUAL_UINT32(expected_rx_producer, driver.rx_ring.producer);
    TEST_ASSERT_EQUAL_UINT32(expected_rx_producer - 20u, driver.rx_ring.consumer);
    TEST_ASSERT_EQUAL_UINT32(108u, driver.uart_rx_bytes);
    TEST_ASSERT_EQUAL_UINT32(0u, driver.rx_dma_last_progress);
    for (uint32_t index = 0u; index < 20u; ++index) {
        TEST_ASSERT_EQUAL_UINT8(0xa0u + index,
            driver.rx_storage[(driver.rx_ring.consumer + index) & driver.rx_ring.mask]);
    }
    TEST_ASSERT_EQUAL_UINT(1u, rx_abort_calls);
    TEST_ASSERT_EQUAL_UINT(1u, rx_configure_calls);
    TEST_ASSERT_EQUAL_UINT(2u, rx_ack_calls);
    TEST_ASSERT_TRUE(rx_dma_busy);
    TEST_ASSERT_TRUE(rx_irq_enabled);
    TEST_ASSERT_EQUAL_PTR(&driver, pio_uart_driver_rx_irq_owners[2]);
    TEST_ASSERT_TRUE(sm_enabled[config.tx_state_machine]);
    TEST_ASSERT_TRUE(sm_enabled[config.rx_state_machine]);
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
    claimed_state_machines = 0u;
    claimed_dma_channels = 0u;
    memset(&driver, 0, sizeof(driver));
    memset(&pio_tx_test_pio0, 0, sizeof(pio_tx_test_pio0));
    memset(&dma_registers, 0, sizeof(dma_registers));
    memset(pio_uart_program_state, 0, sizeof(pio_uart_program_state));
    memset(pio_uart_driver_rx_irq_owners, 0, sizeof(pio_uart_driver_rx_irq_owners));
    memset(clear_calls, 0, sizeof(clear_calls));
    memset(restart_calls, 0, sizeof(restart_calls));
    memset(dividers, 0, sizeof(dividers));
    memset(sm_enabled, 0, sizeof(sm_enabled));
    driver.config = &config;
    driver.baud_rate = 115200u;
    driver.initialized = true;
    driver.tx_dma_channel = 3;
    driver.rx_dma_channel = 2;
    TEST_ASSERT_TRUE(ring_buffer_init(&driver.rx_ring, driver.rx_storage, sizeof(driver.rx_storage)));
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
    rx_progress = expected_rx_producer = 0u;
    rx_configure_calls = rx_abort_calls = rx_ack_calls = pause_calls = divider_calls = divider_restart_calls = 0u;
    late_rx_condition = PIO_TEST_LATE_NONE;
    interrupts_enabled = rx_irq_enabled = rx_dma_busy = rx_fifo_empty = rx_high = progress_settles = true;
    sm_enabled[config.tx_state_machine] = sm_enabled[config.rx_state_machine] = true;
    pio_uart_program_state[0].rx_offset = rx_pc = 7u;
    pio_uart_driver_rx_dma_irq_installed = true;
    pio_uart_driver_rx_irq_owners[2] = &driver;
    dma_hw->ch[2].al1_ctrl = DMA_CH0_CTRL_TRIG_EN_BITS;
    dma_hw->ch[2].transfer_count = uart_dma_rx_transfer_count_encoded();
}

void tearDown(void)
{
    TEST_ASSERT_TRUE(interrupts_enabled);
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

void test_baud_change_preserves_unread_rx_and_applies_both_dividers(void)
{
    seed_baud_change();
    TEST_ASSERT_TRUE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
    assert_rx_preserved_and_resumed();
    TEST_ASSERT_EQUAL_UINT32(230400u, driver.baud_rate);
    TEST_ASSERT_EQUAL_UINT(2u, divider_calls);
    TEST_ASSERT_EQUAL_UINT(1u, divider_restart_calls);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 125000000.0f / (8.0f * 230400.0f), dividers[config.tx_state_machine]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 125000000.0f / (32.0f * 230400.0f), dividers[config.rx_state_machine]);
    TEST_ASSERT_EQUAL_UINT(1u, restart_calls[config.tx_state_machine]);
    TEST_ASSERT_EQUAL_UINT(1u, restart_calls[config.rx_state_machine]);
    TEST_ASSERT_FALSE(driver.txstall_waiting);
}

void test_unsettled_rx_resumes_without_abort_or_final_publication(void)
{
    seed_baud_change();
    progress_settles = false;
    TEST_ASSERT_FALSE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
    TEST_ASSERT_EQUAL_UINT32(expected_rx_producer - 4u, driver.rx_ring.producer);
    TEST_ASSERT_EQUAL_UINT32(24u, driver.rx_dma_last_progress);
    TEST_ASSERT_EQUAL_UINT32(104u, driver.uart_rx_bytes);
    TEST_ASSERT_EQUAL_UINT32(115200u, driver.baud_rate);
    TEST_ASSERT_EQUAL_UINT(1u, pause_calls);
    TEST_ASSERT_EQUAL_UINT(0u, rx_abort_calls);
    TEST_ASSERT_EQUAL_UINT(0u, rx_configure_calls);
    TEST_ASSERT_EQUAL_UINT(0u, divider_calls);
    TEST_ASSERT_TRUE(rx_irq_enabled);
    TEST_ASSERT_TRUE((dma_hw->ch[2].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
    TEST_ASSERT_TRUE(sm_enabled[config.rx_state_machine]);
}

void test_late_rx_or_fifo_activity_restores_service_without_changing_baud(void)
{
    for (uint condition = PIO_TEST_LATE_RX_FIFO; condition <= PIO_TEST_LATE_TX_FIFO; ++condition) {
        setUp();
        seed_baud_change();
        late_rx_condition = (pio_test_late_condition_t)condition;
        TEST_ASSERT_FALSE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
        assert_rx_preserved_and_resumed();
        TEST_ASSERT_EQUAL_UINT32(115200u, driver.baud_rate);
        TEST_ASSERT_EQUAL_UINT(0u, divider_calls);
        TEST_ASSERT_EQUAL_UINT(0u, divider_restart_calls);
        TEST_ASSERT_EQUAL_UINT(0u, clear_calls[config.rx_state_machine]);
        TEST_ASSERT_EQUAL_UINT(0u, restart_calls[config.rx_state_machine]);
        TEST_ASSERT_TRUE(interrupts_enabled);
    }
}

void test_baud_change_rejects_busy_tx_or_nonidle_rx_before_pausing_dma(void)
{
    seed_baud_change();
    rx_high = false;
    TEST_ASSERT_FALSE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
    rx_high = true;
    ++rx_pc;
    TEST_ASSERT_FALSE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
    --rx_pc;
    rx_fifo_empty = false;
    TEST_ASSERT_FALSE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
    rx_fifo_empty = true;
    fifo_level = 1u;
    TEST_ASSERT_FALSE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
    fifo_level = 0u;
    driver.tx_dma_active = dma_busy = true;
    TEST_ASSERT_FALSE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
    TEST_ASSERT_EQUAL_UINT(0u, pause_calls);
    TEST_ASSERT_EQUAL_UINT(0u, rx_abort_calls);
    TEST_ASSERT_EQUAL_UINT(0u, divider_calls);
}

void test_baud_change_waits_for_fresh_txstall_across_calls(void)
{
    seed_baud_change();
    driver.txstall_waiting = false;
    TEST_ASSERT_FALSE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
    TEST_ASSERT_TRUE(driver.txstall_waiting);
    TEST_ASSERT_EQUAL_UINT(0u, pause_calls);
    config.pio->fdebug = 0u;
    TEST_ASSERT_FALSE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
    TEST_ASSERT_EQUAL_UINT(0u, pause_calls);
    config.pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + config.tx_state_machine);
    TEST_ASSERT_TRUE(pio_uart_driver_apply_baud_rate(&driver, 230400u));
    assert_rx_preserved_and_resumed();
}

void test_released_pio_backend_parks_tx_idle_high(void)
{
    parked_tx_high = false;
    parked_tx_output = false;
    parked_tx_function = 99u;
    pio_uart_driver_release_gpio(&driver);
    TEST_ASSERT_TRUE(parked_tx_high);
    TEST_ASSERT_TRUE(parked_tx_output);
    TEST_ASSERT_EQUAL_UINT(GPIO_FUNC_SIO, parked_tx_function);
}

void test_production_resource_bindings_delegate_to_sdk(void)
{
    const pio_uart_resource_claim_ops_t *ops = &pio_uart_resource_claim_ops_default;

    TEST_ASSERT_FALSE(ops->sm_is_claimed(config.pio, config.tx_state_machine));
    ops->sm_claim(config.pio, config.tx_state_machine);
    TEST_ASSERT_TRUE(ops->sm_is_claimed(config.pio, config.tx_state_machine));
    ops->sm_unclaim(config.pio, config.tx_state_machine);
    TEST_ASSERT_FALSE(ops->sm_is_claimed(config.pio, config.tx_state_machine));
    TEST_ASSERT_EQUAL_INT(2, ops->claim_dma_channel(false));
    TEST_ASSERT_EQUAL_INT(3, ops->claim_dma_channel(false));
    TEST_ASSERT_EQUAL_INT(-1, ops->claim_dma_channel(false));
    ops->unclaim_dma_channel(3u);
    ops->unclaim_dma_channel(2u);
    TEST_ASSERT_EQUAL_UINT(0u, claimed_dma_channels);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_production_resource_bindings_delegate_to_sdk);
    RUN_TEST(test_released_pio_backend_parks_tx_idle_high);
    RUN_TEST(test_fifo_launch_invalidates_probe_before_writing_bytes);
    RUN_TEST(test_empty_and_full_fifo_paths_do_not_invalidate_probe);
    RUN_TEST(test_dma_completion_relaunch_preserves_order_and_requires_fresh_idle);
    RUN_TEST(test_dma_relaunch_uses_new_span_after_ring_wrap);
    RUN_TEST(test_baud_change_preserves_unread_rx_and_applies_both_dividers);
    RUN_TEST(test_unsettled_rx_resumes_without_abort_or_final_publication);
    RUN_TEST(test_late_rx_or_fifo_activity_restores_service_without_changing_baud);
    RUN_TEST(test_baud_change_rejects_busy_tx_or_nonidle_rx_before_pausing_dma);
    RUN_TEST(test_baud_change_waits_for_fresh_txstall_across_calls);
    return UNITY_END();
}