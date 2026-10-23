/**
 * @file test_hw_uart_driver.c
 * @brief Execute real HW UART DMA and reconfiguration paths with SDK register-call mocks.
 */

#include "unity.h"
#include <string.h>

#ifndef HW_UART_DRIVER_SOURCE
#define HW_UART_DRIVER_SOURCE "../src/uart/hw/hw_uart_driver.c"
#endif
#include HW_UART_DRIVER_SOURCE

uart_inst_t hw_test_uart0;
uart_inst_t hw_test_uart1;
static pio_test_dma_hw_t dma_registers;
pio_test_dma_hw_t *dma_hw = &dma_registers;
static hw_uart_driver_t driver;
static uint8_t rx_storage[PICO_UART_HW_UART_RX_BUFFER_SIZE]
    __attribute__((aligned(PICO_UART_HW_UART_RX_BUFFER_SIZE)));
static const uart_driver_port_info_t port = {
    .id = UART_PORT_0, .backend = UART_DRIVER_BACKEND_HW, .baud_rate = 115200u,
    .tx_pin = 0u, .rx_pin = 1u, .rts_pin = 3u, .cts_pin = 2u,
    .data_bits = 8u, .stop_bits = 1u, .parity = UART_DRIVER_PARITY_NONE,
};
static hw_uart_driver_config_t config = {.port = &port, .instance = uart0};
static dma_channel_config settings[12];
static uint configure_calls[12];
static uint abort_calls[12];
static uint unclaim_calls[12];
static uint rx_rearm_calls;
static uint rx_irq_acknowledgements;
static bool busy[12];
static bool irq_enabled[12];
static bool irq_pending[12];
static const volatile uint8_t *read_address[12];
static volatile uint8_t *write_address[12];
static uint32_t count[12];
static uint claims;
static uint launches;
static uint uart_reads;
static uint gpio_put_calls;
static bool gpio_put_value;
static uint tx_gpio_function;
static bool tx_gpio_output;
static bool tx_gpio_high;
static uint clock_calls;
static uint init_calls;
static uint deinit_calls;
static uint32_t rx_progress;
static uint32_t producer_at_abort;
static bool interrupts_enabled;
static bool settle_success;
static bool rx_arrives_after_abort;
static bool reject_new_clock;
static bool reject_restore_clock;

int dma_claim_unused_channel(bool required)
{
    TEST_ASSERT_FALSE(required);
    return (int)(2u + claims++);
}

void service_load_note(unsigned int core)
{
    TEST_ASSERT_EQUAL_UINT(1u, core);
}

uint32_t clock_get_hz(uint clock)
{
    TEST_ASSERT_EQUAL_UINT(clk_peri, clock);
    ++clock_calls;
    if ((clock_calls == 2u && reject_new_clock) || (clock_calls >= 3u && reject_restore_clock)) {
        return 0u;
    }
    return 125000000u;
}

uart_hw_t *uart_get_hw(uart_inst_t *uart)
{
    TEST_ASSERT_EQUAL_PTR(uart0, uart);
    ++uart_reads;
    return &uart->registers;
}

uint32_t uart_init(uart_inst_t *uart, uint32_t baud_rate)
{
    uint32_t actual_rate;
    TEST_ASSERT_EQUAL_PTR(uart0, uart);
    TEST_ASSERT_TRUE(interrupts_enabled);
    TEST_ASSERT_TRUE(hw_uart_baud_rate_supported(baud_rate, 125000000u, &actual_rate));
    ++init_calls;
    return actual_rate;
}

void uart_deinit(uart_inst_t *uart)
{
    TEST_ASSERT_EQUAL_PTR(uart0, uart);
    ++deinit_calls;
}

uint uart_get_dreq(uart_inst_t *uart, bool tx)
{
    TEST_ASSERT_EQUAL_PTR(uart0, uart);
    return tx ? 11u : 10u;
}

bool uart_is_readable(uart_inst_t *uart)
{
    TEST_ASSERT_EQUAL_PTR(uart0, uart);
    return rx_arrives_after_abort && abort_calls[2] != 0u;
}

void uart_set_hw_flow(uart_inst_t *uart, bool cts, bool rts)
{
    TEST_ASSERT_EQUAL_PTR(uart0, uart);
    TEST_ASSERT_FALSE(cts);
    TEST_ASSERT_FALSE(rts);
}

void uart_set_format(uart_inst_t *uart, uint data_bits, uint stop_bits, uart_parity_t parity)
{
    TEST_ASSERT_EQUAL_PTR(uart0, uart);
    TEST_ASSERT_EQUAL_UINT(driver.data_bits, data_bits);
    TEST_ASSERT_EQUAL_UINT(driver.stop_bits, stop_bits);
    TEST_ASSERT_EQUAL_UINT(driver.parity, parity);
}

void uart_set_fifo_enabled(uart_inst_t *uart, bool enabled)
{
    TEST_ASSERT_EQUAL_PTR(uart0, uart);
    TEST_ASSERT_TRUE(enabled);
}

void gpio_set_function(uint pin, uint function) { if (pin == port.tx_pin) tx_gpio_function = function; }
void gpio_set_slew_rate(uint pin, uint rate) { (void)pin; (void)rate; }
void gpio_set_drive_strength(uint pin, uint strength) { (void)pin; (void)strength; }
void gpio_pull_down(uint pin) { (void)pin; }
void gpio_disable_pulls(uint pin) { (void)pin; }
void gpio_put(uint pin, bool value)
{
    if (pin == port.tx_pin) tx_gpio_high = value;
    gpio_put_value = value;
    ++gpio_put_calls;
}
void gpio_set_dir(uint pin, bool output) { if (pin == port.tx_pin) tx_gpio_output = output; }

uint32_t save_and_disable_interrupts(void)
{
    uint32_t previous = interrupts_enabled;
    interrupts_enabled = false;
    return previous;
}

void restore_interrupts(uint32_t previous) { interrupts_enabled = previous != 0u; }

void hw_clear_bits(volatile uint32_t *address, uint32_t mask) { *address &= ~mask; }
void hw_set_bits(volatile uint32_t *address, uint32_t mask) { *address |= mask; }

dma_channel_config dma_channel_get_default_config(uint channel)
{
    TEST_ASSERT_TRUE(channel == 2u || channel == 3u);
    return (dma_channel_config){.enabled = true};
}

void channel_config_set_transfer_data_size(dma_channel_config *value, uint size) { value->data_size = size; }
void channel_config_set_read_increment(dma_channel_config *value, bool enabled) { value->read_increment = enabled; }
void channel_config_set_write_increment(dma_channel_config *value, bool enabled) { value->write_increment = enabled; }
void channel_config_set_dreq(dma_channel_config *value, uint dreq) { value->dreq = dreq; }
void channel_config_set_irq_quiet(dma_channel_config *value, bool quiet) { (void)value; TEST_ASSERT_FALSE(quiet); }

void channel_config_set_ring(dma_channel_config *value, bool write, uint bits)
{
    (void)value;
    TEST_ASSERT_TRUE(write);
    TEST_ASSERT_EQUAL_UINT(PICO_UART_HW_UART_RX_DMA_RING_BITS, bits);
}

void dma_channel_configure(uint channel, const dma_channel_config *value, volatile void *destination,
                           const volatile void *source, uint32_t transfer_count, bool trigger)
{
    TEST_ASSERT_TRUE(channel == 2u || channel == 3u);
    ++configure_calls[channel];
    settings[channel] = *value;
    dma_hw->ch[channel].al1_ctrl = value->enabled ? DMA_CH0_CTRL_TRIG_EN_BITS : 0u;
    write_address[channel] = destination;
    read_address[channel] = source;
    count[channel] = transfer_count;
    dma_hw->ch[channel].transfer_count = transfer_count;
    busy[channel] = trigger;
    if (channel == 2u) {
        TEST_ASSERT_TRUE(trigger);
        TEST_ASSERT_EQUAL_PTR(&driver.rx_storage[driver.rx_ring.producer & driver.rx_ring.mask], destination);
        rx_progress = 0u;
    } else {
        TEST_ASSERT_FALSE(trigger);
        TEST_ASSERT_NULL((const void *)source);
        TEST_ASSERT_EQUAL_UINT32(0u, transfer_count);
        TEST_ASSERT_EQUAL_PTR(&hw_test_uart0.registers.dr, destination);
    }
}

void dma_channel_set_read_addr(uint channel, const volatile void *address, bool trigger)
{
    TEST_ASSERT_EQUAL_UINT(3u, channel);
    TEST_ASSERT_FALSE(trigger);
    TEST_ASSERT_FALSE(busy[channel]);
    read_address[channel] = address;
}

void dma_channel_set_trans_count(uint channel, uint32_t transfer_count, bool trigger)
{
    if (channel == 2u) {
        TEST_ASSERT_TRUE(trigger);
        TEST_ASSERT_TRUE((dma_hw->ch[channel].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
        TEST_ASSERT_EQUAL_PTR(&driver, hw_uart_driver_rx_irq_owners[channel]);
        count[channel] = transfer_count;
        dma_hw->ch[channel].transfer_count = transfer_count;
        busy[channel] = true;
        ++rx_rearm_calls;
        return;
    }

    TEST_ASSERT_EQUAL_UINT(3u, channel);
    TEST_ASSERT_TRUE(trigger);
    TEST_ASSERT_TRUE((dma_hw->ch[channel].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
    TEST_ASSERT_TRUE(driver.tx_active);
    TEST_ASSERT_EQUAL_UINT(transfer_count, driver.tx_dma_bytes_in_flight);
    TEST_ASSERT_EQUAL_PTR(&driver.tx_storage[driver.tx_ring.consumer & driver.tx_ring.mask],
                          (const void *)read_address[channel]);
    count[channel] = transfer_count;
    busy[channel] = true;
    ++launches;
}

bool dma_channel_is_busy(uint channel) { return busy[channel]; }

void dma_channel_abort(uint channel)
{
    TEST_ASSERT_FALSE((dma_hw->ch[channel].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
    if (channel == 2u) {
        if (producer_at_abort != UINT32_MAX) {
            TEST_ASSERT_EQUAL_UINT32(producer_at_abort, driver.rx_ring.producer);
        }
        rx_progress = 0xdeadbeefu;
    }
    ++abort_calls[channel];
    busy[channel] = false;
}

void dma_channel_unclaim(uint channel) { ++unclaim_calls[channel]; }
void dma_irqn_acknowledge_channel(uint irq, uint channel)
{
    TEST_ASSERT_EQUAL_UINT(0u, irq);
    irq_pending[channel] = false;
    if (channel == 2u) {
        ++rx_irq_acknowledgements;
    }
}
bool dma_irqn_get_channel_status(uint irq, uint channel)
{
    TEST_ASSERT_EQUAL_UINT(0u, irq);
    return irq_pending[channel];
}
void dma_irqn_set_channel_enabled(uint irq, uint channel, bool enabled)
{
    TEST_ASSERT_EQUAL_UINT(0u, irq);
    irq_enabled[channel] = enabled;
}

uint32_t uart_dma_rx_progress(uint channel) { TEST_ASSERT_EQUAL_UINT(2u, channel); return rx_progress; }
uint32_t uart_dma_rx_transfer_count_max(void) { return UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040; }
uint32_t uart_dma_rx_transfer_count_encoded(void) { return UART_DMA_RX_TRANSFER_COUNT_MAX_RP2040; }
uint32_t uart_dma_rx_transfer_count_remaining(uint channel) { return dma_hw->ch[channel].transfer_count; }
bool uart_dma_rx_wait_paused_progress_stable(uint channel)
{
    TEST_ASSERT_EQUAL_UINT(2u, channel);
    TEST_ASSERT_TRUE(interrupts_enabled);
    TEST_ASSERT_FALSE(irq_enabled[channel]);
    TEST_ASSERT_FALSE((dma_hw->ch[channel].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
    return settle_success;
}

static void queue_bytes(size_t length)
{
    uint8_t data[1024] = {0};
    TEST_ASSERT_TRUE(length <= sizeof(data));
    TEST_ASSERT_EQUAL_UINT(length, ring_buffer_write(&driver.tx_ring, data, length));
}

static void seed_unread_rx(void)
{
    driver.rx_ring.producer = 16u;
    driver.rx_ring.consumer = 4u;
    driver.rx_dma_last_progress = 20u;
    rx_progress = 24u;
    producer_at_abort = 20u;
    clock_calls = 0u;
}

static void complete_rx_dma(void)
{
    busy[2] = false;
    count[2] = 0u;
    dma_hw->ch[2].transfer_count = 0u;
    irq_pending[2] = true;
    rx_progress = 0u;
}

static void assert_rx_dma_rearmed_once(void)
{
    TEST_ASSERT_EQUAL_UINT(1u, rx_rearm_calls);
    TEST_ASSERT_TRUE(busy[2]);
    TEST_ASSERT_EQUAL_UINT32(uart_dma_rx_transfer_count_encoded(), count[2]);
    TEST_ASSERT_EQUAL_UINT32(count[2], dma_hw->ch[2].transfer_count);
    TEST_ASSERT_TRUE((dma_hw->ch[2].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
    TEST_ASSERT_FALSE(irq_pending[2]);
    TEST_ASSERT_EQUAL_UINT(1u, rx_irq_acknowledgements);
}

void setUp(void)
{
    memset(&driver, 0, sizeof(driver));
    driver.rx_storage = rx_storage;
    memset(&hw_test_uart0, 0, sizeof(hw_test_uart0));
    memset(&dma_registers, 0, sizeof(dma_registers));
    memset(settings, 0, sizeof(settings));
    memset(configure_calls, 0, sizeof(configure_calls));
    memset(abort_calls, 0, sizeof(abort_calls));
    memset(unclaim_calls, 0, sizeof(unclaim_calls));
    rx_rearm_calls = rx_irq_acknowledgements = 0u;
    memset(busy, 0, sizeof(busy));
    memset(irq_enabled, 0, sizeof(irq_enabled));
    memset(irq_pending, 0, sizeof(irq_pending));
    memset(hw_uart_driver_rx_irq_owners, 0, sizeof(hw_uart_driver_rx_irq_owners));
    claims = launches = uart_reads = clock_calls = init_calls = deinit_calls = 0u;
    gpio_put_calls = 0u;
    gpio_put_value = false;
    rx_progress = 0u;
    producer_at_abort = UINT32_MAX;
    interrupts_enabled = true;
    settle_success = true;
    rx_arrives_after_abort = reject_new_clock = reject_restore_clock = false;
    hw_uart_driver_rx_dma_irq_installed = true;
    driver.config = &config;
    driver.baud_rate = port.baud_rate;
    driver.data_bits = port.data_bits;
    driver.stop_bits = port.stop_bits;
    driver.parity = UART_PARITY_NONE;
    config.hardware_flow_control = false;
    TEST_ASSERT_TRUE(hw_uart_driver_start(&driver));
    rx_rearm_calls = rx_irq_acknowledgements = 0u;
    clock_calls = 0u;
}

void tearDown(void) { TEST_ASSERT_TRUE(interrupts_enabled); }

void test_start_rejects_missing_or_misaligned_rx_storage_before_claiming_dma(void)
{
    hw_uart_driver_deinit(&driver);
    uint claims_before = claims;
    driver.rx_storage = NULL;
    TEST_ASSERT_FALSE(hw_uart_driver_start(&driver));
    driver.rx_storage = rx_storage + 1u;
    TEST_ASSERT_FALSE(hw_uart_driver_start(&driver));
    TEST_ASSERT_EQUAL_UINT(claims_before, claims);
    TEST_ASSERT_FALSE(driver.initialized);
}

void test_tx_commits_only_after_completion_and_relaunches_in_order(void)
{
    queue_bytes(800u);
    TEST_ASSERT_TRUE(hw_uart_driver_start_tx_dma(&driver));
    uint32_t first = count[3];
    TEST_ASSERT_TRUE(first > 0u && first < 800u);
    TEST_ASSERT_TRUE(driver.tx_active);
    TEST_ASSERT_EQUAL_UINT32(0u, driver.tx_ring.consumer);
    hw_uart_driver_poll(&driver, false);
    TEST_ASSERT_EQUAL_UINT32(0u, driver.tx_ring.consumer);
    TEST_ASSERT_EQUAL_UINT(1u, launches);
    busy[3] = false;
    hw_uart_driver_poll(&driver, false);
    TEST_ASSERT_FALSE(driver.tx_active);
    TEST_ASSERT_EQUAL_UINT32(first, driver.tx_ring.consumer);
    TEST_ASSERT_EQUAL_UINT32(first, driver.uart_tx_bytes);
    hw_uart_driver_poll(&driver, true);
    TEST_ASSERT_EQUAL_UINT(2u, launches);
    TEST_ASSERT_EQUAL_PTR(&driver.tx_storage[first], (const void *)read_address[3]);
    TEST_ASSERT_EQUAL_UINT(1u, configure_calls[3]);
    TEST_ASSERT_TRUE(settings[3].enabled);
    TEST_ASSERT_TRUE(settings[3].read_increment);
    TEST_ASSERT_FALSE(settings[3].write_increment);
    TEST_ASSERT_EQUAL_UINT(DMA_SIZE_8, settings[3].data_size);
    TEST_ASSERT_EQUAL_UINT(11u, settings[3].dreq);
}

void test_empty_tx_queue_skips_uart_busy_register_read(void)
{
    uart_reads = 0u;
    TEST_ASSERT_FALSE(hw_uart_driver_start_tx_dma(&driver));
    TEST_ASSERT_EQUAL_UINT(0u, uart_reads);
}

void test_rts_runtime_updates_use_hysteresis_and_only_write_on_transition(void)
{
    size_t high_watermark = ((size_t)driver.rx_ring.size * 3u) / 4u;
    size_t low_watermark = (size_t)driver.rx_ring.size / 2u;

    config.hardware_flow_control = true;
    driver.rx_rts_asserted = true;
    driver.rx_ring.producer = (uint32_t)high_watermark;
    driver.rx_ring.consumer = 0u;
    hw_uart_driver_update_rts(&driver);
    TEST_ASSERT_FALSE(driver.rx_rts_asserted);
    TEST_ASSERT_EQUAL_UINT(1u, gpio_put_calls);
    TEST_ASSERT_TRUE(gpio_put_value);

    hw_uart_driver_update_rts(&driver);
    TEST_ASSERT_EQUAL_UINT(1u, gpio_put_calls);

    driver.rx_ring.producer = (uint32_t)low_watermark;
    hw_uart_driver_update_rts(&driver);
    TEST_ASSERT_TRUE(driver.rx_rts_asserted);
    TEST_ASSERT_EQUAL_UINT(2u, gpio_put_calls);
    TEST_ASSERT_FALSE(gpio_put_value);
}

void test_completed_transfer_has_only_one_launch_attempt_per_poll(void)
{
    queue_bytes(800u);
    TEST_ASSERT_TRUE(hw_uart_driver_start_tx_dma(&driver));
    busy[3] = false;
    hw_test_uart0.registers.fr = UART_UARTFR_BUSY_BITS;
    uart_reads = 0u;
    hw_uart_driver_poll(&driver, true);
    TEST_ASSERT_FALSE(driver.tx_active);
    TEST_ASSERT_EQUAL_UINT(1u, launches);
    TEST_ASSERT_EQUAL_UINT(2u, uart_reads);
}

void test_uart_busy_blocks_launch_and_format_change(void)
{
    queue_bytes(10u);
    hw_test_uart0.registers.fr = UART_UARTFR_BUSY_BITS;
    TEST_ASSERT_FALSE(hw_uart_driver_start_tx_dma(&driver));
    TEST_ASSERT_FALSE(hw_uart_driver_apply_line_format(&driver, 230400u, 7u, 2u, UART_PARITY_EVEN));
    TEST_ASSERT_EQUAL_UINT(0u, launches);
    TEST_ASSERT_EQUAL_UINT(0u, abort_calls[2]);
    TEST_ASSERT_EQUAL_UINT(0u, deinit_calls);
}

void test_successful_format_change_preserves_rx_and_can_transmit_again(void)
{
    seed_unread_rx();
    TEST_ASSERT_TRUE(hw_uart_driver_apply_line_format(&driver, 230400u, 7u, 2u, UART_PARITY_EVEN));
    TEST_ASSERT_TRUE(driver.initialized);
    TEST_ASSERT_EQUAL_UINT8(7u, driver.data_bits);
    TEST_ASSERT_EQUAL_UINT8(2u, driver.stop_bits);
    TEST_ASSERT_EQUAL_UINT(UART_PARITY_EVEN, driver.parity);
    TEST_ASSERT_EQUAL_UINT32(20u, driver.rx_ring.producer);
    TEST_ASSERT_EQUAL_UINT32(4u, driver.rx_ring.consumer);
    TEST_ASSERT_EQUAL_UINT(1u, abort_calls[3]);
    TEST_ASSERT_TRUE(irq_enabled[2]);
    TEST_ASSERT_FALSE((dma_hw->ch[3].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
    queue_bytes(10u);
    TEST_ASSERT_TRUE(hw_uart_driver_start_tx_dma(&driver));
    TEST_ASSERT_TRUE((dma_hw->ch[3].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
    TEST_ASSERT_EQUAL_UINT(1u, configure_calls[3]);
}

void test_format_failure_restores_previous_settings_and_tx_service(void)
{
    seed_unread_rx();
    uint32_t old_rate = driver.baud_rate;
    reject_new_clock = true;
    TEST_ASSERT_FALSE(hw_uart_driver_apply_line_format(&driver, 230400u, 7u, 2u, UART_PARITY_ODD));
    TEST_ASSERT_TRUE(driver.initialized);
    TEST_ASSERT_EQUAL_UINT32(old_rate, driver.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(8u, driver.data_bits);
    TEST_ASSERT_EQUAL_UINT8(1u, driver.stop_bits);
    TEST_ASSERT_EQUAL_UINT(UART_PARITY_NONE, driver.parity);
    TEST_ASSERT_EQUAL_UINT32(20u, driver.rx_ring.producer);
    TEST_ASSERT_EQUAL_UINT32(4u, driver.rx_ring.consumer);
    TEST_ASSERT_TRUE(irq_enabled[2]);
    TEST_ASSERT_FALSE((dma_hw->ch[3].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
    queue_bytes(10u);
    TEST_ASSERT_TRUE(hw_uart_driver_start_tx_dma(&driver));
    TEST_ASSERT_EQUAL_UINT(1u, configure_calls[3]);
}

void test_failure_to_restore_releases_channels_and_disables_port(void)
{
    seed_unread_rx();
    reject_new_clock = true;
    reject_restore_clock = true;
    TEST_ASSERT_FALSE(hw_uart_driver_apply_line_format(&driver, 230400u, 8u, 1u, UART_PARITY_NONE));
    TEST_ASSERT_FALSE(driver.initialized);
    TEST_ASSERT_EQUAL_INT(-1, driver.rx_dma_channel);
    TEST_ASSERT_EQUAL_INT(-1, driver.tx_dma_channel);
    TEST_ASSERT_EQUAL_UINT(1u, unclaim_calls[2]);
    TEST_ASSERT_EQUAL_UINT(1u, unclaim_calls[3]);
    TEST_ASSERT_NULL(hw_uart_driver_rx_irq_owners[2]);
    TEST_ASSERT_FALSE(irq_enabled[2]);
    TEST_ASSERT_EQUAL_UINT(GPIO_FUNC_SIO, tx_gpio_function);
    TEST_ASSERT_TRUE(tx_gpio_output);
    TEST_ASSERT_TRUE(tx_gpio_high);
}

void test_unsettled_rx_resumes_without_abort_or_publication(void)
{
    seed_unread_rx();
    settle_success = false;
    TEST_ASSERT_FALSE(hw_uart_driver_apply_line_format(&driver, 230400u, 8u, 1u, UART_PARITY_NONE));
    TEST_ASSERT_EQUAL_UINT32(16u, driver.rx_ring.producer);
    TEST_ASSERT_EQUAL_UINT32(20u, driver.rx_dma_last_progress);
    TEST_ASSERT_EQUAL_UINT(0u, abort_calls[2]);
    TEST_ASSERT_EQUAL_UINT(0u, deinit_calls);
    TEST_ASSERT_TRUE(irq_enabled[2]);
    TEST_ASSERT_TRUE((dma_hw->ch[2].al1_ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) != 0u);
}

void test_rx_arriving_during_pause_defers_without_aborting_tx(void)
{
    seed_unread_rx();
    rx_arrives_after_abort = true;
    TEST_ASSERT_FALSE(hw_uart_driver_apply_line_format(&driver, 230400u, 8u, 1u, UART_PARITY_NONE));
    TEST_ASSERT_TRUE(driver.initialized);
    TEST_ASSERT_EQUAL_UINT32(20u, driver.rx_ring.producer);
    TEST_ASSERT_EQUAL_UINT32(4u, driver.rx_ring.consumer);
    TEST_ASSERT_EQUAL_UINT(0u, abort_calls[3]);
    TEST_ASSERT_EQUAL_UINT(0u, deinit_calls);
    TEST_ASSERT_TRUE(irq_enabled[2]);
}

void test_rx_dma_irq_rearms_exhausted_transfer_once(void)
{
    complete_rx_dma();
    hw_uart_driver_rx_dma_irq_handler();
    assert_rx_dma_rearmed_once();

    hw_uart_driver_rx_dma_irq_handler();
    TEST_ASSERT_EQUAL_UINT(1u, rx_rearm_calls);
}

void test_rx_dma_poll_fallback_acknowledges_before_delayed_irq(void)
{
    complete_rx_dma();
    hw_uart_driver_poll(&driver, false);
    assert_rx_dma_rearmed_once();

    hw_uart_driver_rx_dma_irq_handler();
    TEST_ASSERT_EQUAL_UINT(1u, rx_rearm_calls);
}

void test_closed_hw_backend_parks_tx_idle_high(void)
{
    tx_gpio_function = GPIO_FUNC_UART;
    tx_gpio_output = false;
    tx_gpio_high = false;
    hw_uart_driver_deinit(&driver);
    TEST_ASSERT_EQUAL_UINT(GPIO_FUNC_SIO, tx_gpio_function);
    TEST_ASSERT_TRUE(tx_gpio_output);
    TEST_ASSERT_TRUE(tx_gpio_high);
    TEST_ASSERT_FALSE(driver.initialized);
}

static void test_restart_baseline_preserves_observed_error_history(void)
{
    driver.rx_error_count = 7u;
    hw_test_uart0.registers.rsr = 15u;
    hw_uart_driver_clear_rx_error_baseline(&driver);
    TEST_ASSERT_EQUAL_UINT32(7u, driver.rx_error_count);
    TEST_ASSERT_EQUAL_UINT32(0u, hw_test_uart0.registers.rsr);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_restart_baseline_preserves_observed_error_history);
    RUN_TEST(test_start_rejects_missing_or_misaligned_rx_storage_before_claiming_dma);
    RUN_TEST(test_closed_hw_backend_parks_tx_idle_high);
    RUN_TEST(test_tx_commits_only_after_completion_and_relaunches_in_order);
    RUN_TEST(test_empty_tx_queue_skips_uart_busy_register_read);
    RUN_TEST(test_rts_runtime_updates_use_hysteresis_and_only_write_on_transition);
    RUN_TEST(test_completed_transfer_has_only_one_launch_attempt_per_poll);
    RUN_TEST(test_uart_busy_blocks_launch_and_format_change);
    RUN_TEST(test_successful_format_change_preserves_rx_and_can_transmit_again);
    RUN_TEST(test_format_failure_restores_previous_settings_and_tx_service);
    RUN_TEST(test_failure_to_restore_releases_channels_and_disables_port);
    RUN_TEST(test_unsettled_rx_resumes_without_abort_or_publication);
    RUN_TEST(test_rx_arriving_during_pause_defers_without_aborting_tx);
    RUN_TEST(test_rx_dma_irq_rearms_exhausted_transfer_once);
    RUN_TEST(test_rx_dma_poll_fallback_acknowledges_before_delayed_irq);
    return UNITY_END();
}