/**
 * @file driver.c
 * @brief Hardware UART backend for PicoUart logical UART ports.
 */

#include "uart/hw/driver.h"

#include "uart/dma_progress.h"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/regs/uart.h"
#include "hardware/structs/dma.h"
#include "hardware/sync.h"
#include "pico/platform.h"
#include "pico/stdlib.h"

#include <stddef.h>

_Static_assert((offsetof(hw_uart_driver_t, rx_storage) % PICO_UART_HW_UART_RX_BUFFER_SIZE) == 0u,
               "HW RX DMA ring storage must be size-aligned for channel_config_set_ring");

/** @brief Shared DMA IRQ used for HW UART RX transfer-count re-arm. */
#define HW_UART_DRIVER_RX_DMA_IRQ_INDEX 0

/** @brief Drivers that own an RX DMA channel armed on @ref HW_UART_DRIVER_RX_DMA_IRQ_INDEX. */
static hw_uart_driver_t *hw_uart_driver_rx_irq_owners[NUM_DMA_CHANNELS];
/** @brief True after the shared DMA IRQ0 handler has been installed once. */
static bool hw_uart_driver_rx_dma_irq_installed;

static void hw_uart_driver_configure_uart(hw_uart_driver_t *driver)
{
    uart_init(driver->config.instance, driver->config.baud_rate);
    uart_set_hw_flow(driver->config.instance,
                     driver->config.hardware_flow_control,
                     driver->config.hardware_flow_control);
    uart_set_format(driver->config.instance,
                    driver->config.data_bits,
                    driver->config.stop_bits,
                    driver->config.parity);
    uart_set_fifo_enabled(driver->config.instance, true);
}

static void hw_uart_driver_rearm_rx_dma(hw_uart_driver_t *driver)
{
    /*
     * Keep WRITE_ADDR / ring state and only reload TRANS_COUNT. This is the
     * tight path that closes the software re-arm gap after the countdown
     * exhausts. Peers that ignore RTS can still overrun the UART FIFO if
     * re-arm is delayed into the poll loop; the DMA IRQ below restarts before
     * the next worker sweep.
     *
     * Use @ref uart_dma_rx_transfer_count_encoded — raw 0xffffffff is ENDLESS
     * mode on RP2350 and breaks progress publishing.
     */
    dma_channel_set_trans_count((uint)driver->rx_dma_channel,
                                uart_dma_rx_transfer_count_encoded(),
                                true);
}

static void __isr hw_uart_driver_rx_dma_irq_handler(void)
{
    for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
        hw_uart_driver_t *driver = hw_uart_driver_rx_irq_owners[channel];

        if (driver == NULL) {
            continue;
        }

        if (!dma_irqn_get_channel_status(HW_UART_DRIVER_RX_DMA_IRQ_INDEX, channel)) {
            continue;
        }

        dma_irqn_acknowledge_channel(HW_UART_DRIVER_RX_DMA_IRQ_INDEX, channel);
        hw_uart_driver_rearm_rx_dma(driver);
    }
}

void hw_uart_driver_enable_rx_dma_irq(void)
{
    if (!hw_uart_driver_rx_dma_irq_installed) {
        irq_add_shared_handler(DMA_IRQ_0,
                               hw_uart_driver_rx_dma_irq_handler,
                               PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
        irq_set_enabled(DMA_IRQ_0, true);
        hw_uart_driver_rx_dma_irq_installed = true;
    }

    for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
        if (hw_uart_driver_rx_irq_owners[channel] != NULL) {
            dma_irqn_set_channel_enabled(HW_UART_DRIVER_RX_DMA_IRQ_INDEX, channel, true);
        }
    }
}

static void hw_uart_driver_start_rx_dma(hw_uart_driver_t *driver)
{
    dma_channel_config rx_dma_config;
    uint8_t *write_addr;

    rx_dma_config = dma_channel_get_default_config((uint)driver->rx_dma_channel);
    channel_config_set_transfer_data_size(&rx_dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_dma_config, false);
    channel_config_set_write_increment(&rx_dma_config, true);
    channel_config_set_dreq(&rx_dma_config, uart_get_dreq(driver->config.instance, false));
    channel_config_set_ring(&rx_dma_config, true, PICO_UART_HW_UART_RX_DMA_RING_BITS);
    channel_config_set_irq_quiet(&rx_dma_config, false);
    driver->rx_dma_last_progress = 0u;
    /*
     * Continue writing at the live producer index so a line-format restart does
     * not desync DMA WRITE_ADDR from ring_buffer_produce_external accounting.
     */
    write_addr = &driver->rx_storage[ring_buffer_producer_index(&driver->rx_ring)];
    dma_channel_configure((uint)driver->rx_dma_channel,
                          &rx_dma_config,
                          write_addr,
                          &uart_get_hw(driver->config.instance)->dr,
                          uart_dma_rx_transfer_count_encoded(),
                          true);

    hw_uart_driver_rx_irq_owners[driver->rx_dma_channel] = driver;
    dma_irqn_acknowledge_channel(HW_UART_DRIVER_RX_DMA_IRQ_INDEX, (uint)driver->rx_dma_channel);
    if (hw_uart_driver_rx_dma_irq_installed) {
        dma_irqn_set_channel_enabled(HW_UART_DRIVER_RX_DMA_IRQ_INDEX,
                                     (uint)driver->rx_dma_channel,
                                     true);
    }
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

/**
 * @brief Abort one DMA channel safely on RP2040 and RP2350.
 *
 * RP2350-E5: clear channel EN before abort so the controller cannot re-trigger.
 * Use the non-triggering CTRL alias so clearing EN does not start a transfer.
 */
static void hw_uart_driver_abort_dma_channel(uint channel);
static void hw_uart_driver_publish_rx(hw_uart_driver_t *driver);

static void hw_uart_driver_abort_dma_channel(uint channel)
{
    hw_clear_bits(&dma_hw->ch[channel].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort(channel);
}

/**
 * @brief Stop RX DMA for line-format reconfig with a stable progress sample.
 *
 * Clear EN (RP2350-E5 / pause), briefly settle so any in-flight beat can retire,
 * publish progress while the channel is paused, then abort. Do not wait on BUSY
 * after clearing EN: paused channels keep BUSY high until CHAN_ABORT.
 */
static void hw_uart_driver_stop_rx_dma_for_reconfig(hw_uart_driver_t *driver)
{
    uint channel = (uint)driver->rx_dma_channel;

    dma_irqn_set_channel_enabled(HW_UART_DRIVER_RX_DMA_IRQ_INDEX, channel, false);
    hw_clear_bits(&dma_hw->ch[channel].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    for (uint32_t settle = 0u; settle < 16u; ++settle) {
        tight_loop_contents();
    }
    hw_uart_driver_publish_rx(driver);
    dma_channel_abort(channel);
    dma_irqn_acknowledge_channel(HW_UART_DRIVER_RX_DMA_IRQ_INDEX, channel);
}

static void hw_uart_driver_release_dma(hw_uart_driver_t *driver)
{
    if (driver->rx_dma_channel >= 0) {
        dma_irqn_set_channel_enabled(HW_UART_DRIVER_RX_DMA_IRQ_INDEX,
                                     (uint)driver->rx_dma_channel,
                                     false);
        hw_uart_driver_rx_irq_owners[driver->rx_dma_channel] = NULL;
        hw_uart_driver_abort_dma_channel((uint)driver->rx_dma_channel);
        dma_irqn_acknowledge_channel(HW_UART_DRIVER_RX_DMA_IRQ_INDEX,
                                     (uint)driver->rx_dma_channel);
        dma_channel_unclaim((uint)driver->rx_dma_channel);
        driver->rx_dma_channel = -1;
    }

    if (driver->tx_dma_channel >= 0) {
        hw_uart_driver_abort_dma_channel((uint)driver->tx_dma_channel);
        dma_channel_unclaim((uint)driver->tx_dma_channel);
        driver->tx_dma_channel = -1;
    }
}

static uint32_t hw_uart_driver_rx_progress(const hw_uart_driver_t *driver)
{
    return uart_dma_rx_progress((uint)driver->rx_dma_channel);
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
        driver->controller_tx_bytes += (uint32_t)driver->tx_dma_bytes_in_flight;
        driver->tx_active = false;
        driver->tx_dma_bytes_in_flight = 0u;
        (void)hw_uart_driver_start_tx_dma(driver);
    }
}

static void hw_uart_driver_publish_rx(hw_uart_driver_t *driver)
{
    uint32_t progress;
    uint32_t produced;

    if ((driver == NULL) || !driver->initialized) {
        return;
    }

    progress = hw_uart_driver_rx_progress(driver);
    /*
     * After an IRQ (or poll) re-arm, progress drops back toward 0. Treat that as
     * a wrap so bytes between last_progress and the end of the previous countdown
     * are still published.
     */
    produced = uart_dma_rx_bytes_produced(progress,
                                         driver->rx_dma_last_progress,
                                         uart_dma_rx_transfer_count_max());
    driver->controller_rx_bytes += produced;
    driver->rx_dma_last_progress = progress;
    ring_buffer_produce_external(&driver->rx_ring, produced);
}

static void hw_uart_driver_record_rx_errors(hw_uart_driver_t *driver)
{
    uint32_t errors;

    if ((driver == NULL) || !driver->initialized) {
        return;
    }

    errors = uart_get_hw(driver->config.instance)->rsr;
    if ((errors & UART_UARTRSR_BITS) != 0u) {
        driver->rx_error_count += 1u;
        uart_get_hw(driver->config.instance)->rsr = 0u;
    }
}

void hw_uart_driver_poll(hw_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->initialized) {
        return;
    }

    hw_uart_driver_publish_rx(driver);
    hw_uart_driver_record_rx_errors(driver);
    /* Safety net if the DMA IRQ was masked or delayed past transfer completion. */
    if (!dma_channel_is_busy((uint)driver->rx_dma_channel) &&
        (uart_dma_rx_transfer_count_remaining((uint)driver->rx_dma_channel) == 0u)) {
        uint32_t interrupt_status = save_and_disable_interrupts();

        if (!dma_channel_is_busy((uint)driver->rx_dma_channel) &&
            (uart_dma_rx_transfer_count_remaining((uint)driver->rx_dma_channel) == 0u)) {
            hw_uart_driver_rearm_rx_dma(driver);
        }
        restore_interrupts(interrupt_status);
    }
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
        (driver->config.rx_pin == UART_DRIVER_PIN_UNASSIGNED) ||
        (driver->config.hardware_flow_control &&
         ((driver->config.cts_pin == UART_DRIVER_PIN_UNASSIGNED) ||
          (driver->config.rts_pin == UART_DRIVER_PIN_UNASSIGNED)))) {
        return false;
    }

    driver->rx_dma_channel = -1;
    driver->tx_dma_channel = -1;
    driver->tx_dma_bytes_in_flight = 0u;
    driver->tx_active = false;
    driver->controller_tx_bytes = 0u;
    driver->controller_rx_bytes = 0u;
    driver->rx_error_count = 0u;
    driver->rx_dma_last_progress = 0u;

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
    if (driver->config.hardware_flow_control) {
        gpio_set_function(driver->config.cts_pin, GPIO_FUNC_UART);
        gpio_set_function(driver->config.rts_pin, GPIO_FUNC_UART);
        /* CTS is active-low; pull-down keeps TX flowing when the peer omits CTS. */
        gpio_pull_down(driver->config.cts_pin);
    }
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

    /*
     * Do not spin waiting for UARTFR_BUSY. CTS (or a late shifter byte) must
     * defer the apply without stalling the UART worker's RX publish loop.
     * The worker's 1 s deferred-apply deadline fails the request if BUSY sticks.
     */
    if ((uart_get_hw(driver->config.instance)->fr & UART_UARTFR_BUSY_BITS) != 0u) {
        return false;
    }

    /*
     * Mask the RX re-arm IRQ, pause the channel (clear EN), settle briefly,
     * publish a stable progress sample, then abort/ack before restarting.
     */
    {
        uint32_t interrupt_status = save_and_disable_interrupts();

        hw_uart_driver_stop_rx_dma_for_reconfig(driver);
        hw_uart_driver_abort_dma_channel((uint)driver->tx_dma_channel);
        driver->tx_dma_bytes_in_flight = 0u;
        driver->tx_active = false;
        restore_interrupts(interrupt_status);
    }

    /* Preserve unread RX bytes; restart DMA at the live producer index. */
    driver->rx_dma_last_progress = 0u;

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
