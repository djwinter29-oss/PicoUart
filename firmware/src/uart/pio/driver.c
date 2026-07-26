/**
 * @file driver.c
 * @brief PIO UART backend for PicoUart logical UART ports.
 */

#include "uart/pio/internal.h"

#include "uart.pio.h"
#include "uart/dma_progress.h"
#include "uart/line_coding.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/dma.h"
#include "hardware/sync.h"

#include <stddef.h>

_Static_assert((offsetof(pio_uart_driver_t, rx_storage) % PICO_UART_PIO_UART_RX_BUFFER_SIZE) == 0u,
               "PIO RX DMA ring storage must be size-aligned for channel_config_set_ring");

/** @brief Joined PIO TX FIFO depth in 32-bit entries. */
#define PIO_UART_DRIVER_TX_FIFO_DEPTH 8u
/** @brief Maximum bytes launched in one TX DMA transfer. */
#define PIO_UART_DRIVER_DEFAULT_TX_DMA_MAX_TRANSFER_BYTES 256u
/** @brief DMA IRQ used for PIO RX transfer-count re-arm (HW UART owns DMA IRQ0). */
#define PIO_UART_DRIVER_RX_DMA_IRQ_INDEX 1

/**
 * @brief Program load state for one PIO block.
 */
typedef struct {
    bool tx_loaded; /**< True after the TX program is loaded into this PIO block. */
    bool rx_loaded; /**< True after the RX program is loaded into this PIO block. */
    uint tx_offset; /**< Instruction-memory offset for the TX program. */
    uint rx_offset; /**< Instruction-memory offset for the RX program. */
} pio_uart_program_state_t;

static pio_uart_program_state_t pio_uart_program_state[2];
/** @brief Drivers that own an RX DMA channel armed on @ref PIO_UART_DRIVER_RX_DMA_IRQ_INDEX. */
static pio_uart_driver_t *pio_uart_driver_rx_irq_owners[NUM_DMA_CHANNELS];
/** @brief True after the shared DMA IRQ1 handler has been installed once. */
static bool pio_uart_driver_rx_dma_irq_installed;

static void pio_uart_driver_publish_rx(pio_uart_driver_t *driver);
static void pio_uart_driver_harvest_framing_errors(pio_uart_driver_t *driver);
static bool pio_uart_driver_prepare_baud_change_locked(pio_uart_driver_t *driver);
static void pio_uart_driver_apply_baud_locked(pio_uart_driver_t *driver, uint32_t baud_rate);
static void pio_uart_driver_release_dma(pio_uart_driver_t *driver);
static void pio_uart_driver_start_rx_dma(pio_uart_driver_t *driver);
static bool pio_uart_driver_start_tx_dma(pio_uart_driver_t *driver, size_t max_transfer_bytes);
static void pio_uart_driver_poll_tx_dma(pio_uart_driver_t *driver);

static size_t pio_uart_driver_dma_threshold(const pio_uart_driver_t *driver)
{
    if ((driver != NULL) && (driver->config.tx_dma_start_threshold != 0u)) {
        return (size_t)driver->config.tx_dma_start_threshold;
    }
    return PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD;
}

/**
 * @brief Abort one DMA channel safely on RP2040 and RP2350.
 *
 * RP2350-E5: clear channel EN before abort so the controller cannot re-trigger.
 * Use the non-triggering CTRL alias so clearing EN does not start a transfer.
 */
static void pio_uart_driver_abort_dma_channel(uint channel)
{
    hw_clear_bits(&dma_hw->ch[channel].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort(channel);
}

static void pio_uart_driver_rearm_rx_dma(pio_uart_driver_t *driver)
{
    dma_channel_set_trans_count((uint)driver->rx_dma_channel,
                                uart_dma_rx_transfer_count_encoded(),
                                true);
}

static void __isr pio_uart_driver_rx_dma_irq_handler(void)
{
    for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
        pio_uart_driver_t *driver = pio_uart_driver_rx_irq_owners[channel];

        if (driver == NULL) {
            continue;
        }

        if (!dma_irqn_get_channel_status(PIO_UART_DRIVER_RX_DMA_IRQ_INDEX, channel)) {
            continue;
        }

        dma_irqn_acknowledge_channel(PIO_UART_DRIVER_RX_DMA_IRQ_INDEX, channel);
        pio_uart_driver_rearm_rx_dma(driver);
    }
}

static void pio_uart_driver_release_dma(pio_uart_driver_t *driver)
{
    if (driver == NULL) {
        return;
    }

    if (driver->rx_dma_channel >= 0) {
        dma_irqn_set_channel_enabled(PIO_UART_DRIVER_RX_DMA_IRQ_INDEX,
                                     (uint)driver->rx_dma_channel,
                                     false);
        pio_uart_driver_rx_irq_owners[driver->rx_dma_channel] = NULL;
        pio_uart_driver_abort_dma_channel((uint)driver->rx_dma_channel);
        dma_irqn_acknowledge_channel(PIO_UART_DRIVER_RX_DMA_IRQ_INDEX,
                                     (uint)driver->rx_dma_channel);
        dma_channel_unclaim((uint)driver->rx_dma_channel);
        driver->rx_dma_channel = -1;
    }

    if (driver->tx_dma_channel >= 0) {
        pio_uart_driver_abort_dma_channel((uint)driver->tx_dma_channel);
        dma_channel_unclaim((uint)driver->tx_dma_channel);
        driver->tx_dma_channel = -1;
    }

    driver->tx_dma_bytes_in_flight = 0u;
    driver->tx_dma_active = false;
    driver->rx_dma_last_progress = 0u;
}

static uint pio_uart_driver_block_index(PIO pio)
{
    return (pio == pio0) ? 0u : 1u;
}

static float pio_uart_driver_clock_divider(uint32_t baud_rate)
{
    return (float)clock_get_hz(clk_sys) / (8.0f * (float)baud_rate);
}

static bool pio_uart_driver_baud_rate_supported(uint32_t baud_rate)
{
    /* Same integer feasibility gate used by USB fail-fast / Unity tests. */
    return uart_line_coding_pio_baud_feasible(baud_rate, clock_get_hz(clk_sys));
}

static uint pio_uart_driver_tx_offset(PIO pio)
{
    uint block_index = pio_uart_driver_block_index(pio);

    if (!pio_uart_program_state[block_index].tx_loaded) {
        pio_uart_program_state[block_index].tx_offset = pio_add_program(pio, &pio_uart_tx_program);
        pio_uart_program_state[block_index].tx_loaded = true;
    }

    return pio_uart_program_state[block_index].tx_offset;
}

static uint pio_uart_driver_rx_offset(PIO pio)
{
    uint block_index = pio_uart_driver_block_index(pio);

    if (!pio_uart_program_state[block_index].rx_loaded) {
        pio_uart_program_state[block_index].rx_offset = pio_add_program(pio, &pio_uart_rx_program);
        pio_uart_program_state[block_index].rx_loaded = true;
    }

    return pio_uart_program_state[block_index].rx_offset;
}

static void pio_uart_driver_init_tx_sm(pio_uart_driver_t *driver)
{
    uint offset = pio_uart_driver_tx_offset(driver->config.pio);
    pio_sm_config config = pio_uart_tx_program_get_default_config(offset);

    sm_config_set_out_pins(&config, driver->config.tx_pin, 1u);
    sm_config_set_sideset_pins(&config, driver->config.tx_pin);
    sm_config_set_out_shift(&config, true, false, 32u);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&config, pio_uart_driver_clock_divider(driver->config.baud_rate));

    pio_gpio_init(driver->config.pio, driver->config.tx_pin);
    pio_sm_set_consecutive_pindirs(driver->config.pio, driver->config.tx_state_machine, driver->config.tx_pin, 1u, true);
    pio_sm_init(driver->config.pio, driver->config.tx_state_machine, offset, &config);
    pio_sm_set_pins_with_mask(driver->config.pio,
                               driver->config.tx_state_machine,
                               1u << driver->config.tx_pin,
                               1u << driver->config.tx_pin);
    pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, true);
}

static void pio_uart_driver_init_rx_sm(pio_uart_driver_t *driver)
{
    uint offset = pio_uart_driver_rx_offset(driver->config.pio);
    pio_sm_config config = pio_uart_rx_program_get_default_config(offset);

    sm_config_set_in_pins(&config, driver->config.rx_pin);
    /* jmp pin is sampled by the stop-bit check after the 8 data bits. */
    sm_config_set_jmp_pin(&config, driver->config.rx_pin);
    /*
     * Shift right so LSB-first UART samples assemble a natural byte in ISR[31:24].
     * RX DMA reads that high byte via an 8-bit access at rxf+3 (little-endian).
     */
    sm_config_set_in_shift(&config, true, false, 32u);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&config, pio_uart_driver_clock_divider(driver->config.baud_rate));

    pio_gpio_init(driver->config.pio, driver->config.rx_pin);
    if ((driver->config.pin_flags & PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP) != 0u) {
        gpio_pull_up(driver->config.rx_pin);
    }
    pio_sm_set_consecutive_pindirs(driver->config.pio, driver->config.rx_state_machine, driver->config.rx_pin, 1u, false);
    pio_interrupt_clear(driver->config.pio, driver->config.rx_state_machine);
    pio_sm_init(driver->config.pio, driver->config.rx_state_machine, offset, &config);
    pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, true);
}

static void pio_uart_driver_start_rx_dma(pio_uart_driver_t *driver)
{
    dma_channel_config rx_dma_config;
    uint8_t *write_addr;

    rx_dma_config = dma_channel_get_default_config((uint)driver->rx_dma_channel);
    channel_config_set_transfer_data_size(&rx_dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_dma_config, false);
    channel_config_set_write_increment(&rx_dma_config, true);
    channel_config_set_dreq(&rx_dma_config,
                            pio_get_dreq(driver->config.pio,
                                         driver->config.rx_state_machine,
                                         false));
    channel_config_set_ring(&rx_dma_config, true, PICO_UART_PIO_UART_RX_DMA_RING_BITS);
    channel_config_set_irq_quiet(&rx_dma_config, false);
    driver->rx_dma_last_progress = 0u;
    write_addr = &driver->rx_storage[ring_buffer_producer_index(&driver->rx_ring)];
    /*
     * PIO RX FIFO words are 32-bit with the UART byte in bits [31:24] (shift-right
     * IN). An 8-bit DMA from rxf+3 pops one FIFO entry and captures that byte.
     */
    dma_channel_configure(
        (uint)driver->rx_dma_channel,
        &rx_dma_config,
        write_addr,
        (const volatile void *)((uintptr_t)&driver->config.pio->rxf[driver->config.rx_state_machine] +
                                3u),
        uart_dma_rx_transfer_count_encoded(),
        true);

    pio_uart_driver_rx_irq_owners[driver->rx_dma_channel] = driver;
    dma_irqn_acknowledge_channel(PIO_UART_DRIVER_RX_DMA_IRQ_INDEX, (uint)driver->rx_dma_channel);
    dma_irqn_set_channel_enabled(PIO_UART_DRIVER_RX_DMA_IRQ_INDEX,
                                 (uint)driver->rx_dma_channel,
                                 true);
    if (!pio_uart_driver_rx_dma_irq_installed) {
        irq_add_shared_handler(DMA_IRQ_1,
                               pio_uart_driver_rx_dma_irq_handler,
                               PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
        irq_set_enabled(DMA_IRQ_1, true);
        pio_uart_driver_rx_dma_irq_installed = true;
    }
}

static uint32_t pio_uart_driver_rx_progress(const pio_uart_driver_t *driver)
{
    return uart_dma_rx_progress((uint)driver->rx_dma_channel);
}

static void pio_uart_driver_publish_rx(pio_uart_driver_t *driver)
{
    uint32_t progress;
    uint32_t produced;

    if ((driver == NULL) || !driver->initialized || (driver->rx_dma_channel < 0)) {
        return;
    }

    progress = pio_uart_driver_rx_progress(driver);
    produced = uart_dma_rx_bytes_produced(progress,
                                         driver->rx_dma_last_progress,
                                         uart_dma_rx_transfer_count_max());
    driver->controller_rx_bytes += produced;
    driver->rx_dma_last_progress = progress;
    ring_buffer_produce_external(&driver->rx_ring, produced);
}

static void pio_uart_driver_drain_tx_fifo(pio_uart_driver_t *driver)
{
    while (true) {
        ring_buffer_span_t span = ring_buffer_read_span(&driver->tx_ring);
        size_t fifo_headroom = PIO_UART_DRIVER_TX_FIFO_DEPTH -
                               (size_t)pio_sm_get_tx_fifo_level(driver->config.pio,
                                                                driver->config.tx_state_machine);
        size_t chunk;

        if ((span.length == 0u) || (fifo_headroom == 0u)) {
            break;
        }

        chunk = (span.length < fifo_headroom) ? span.length : fifo_headroom;

        for (size_t index = 0u; index < chunk; ++index) {
            pio_sm_put(driver->config.pio,
                       driver->config.tx_state_machine,
                       span.data[index]);
        }

        (void)ring_buffer_commit_consumed(&driver->tx_ring, chunk);
        driver->tx_polled_bytes += chunk;
    }
}

static bool pio_uart_driver_start_tx_dma(pio_uart_driver_t *driver, size_t max_transfer_bytes)
{
    dma_channel_config tx_dma_config;
    ring_buffer_span_t span;
    size_t transfer_length;

    if ((driver == NULL) || (driver->tx_dma_channel >= 0) || driver->tx_dma_active) {
        return false;
    }

    driver->tx_dma_channel = dma_claim_unused_channel(false);
    if (driver->tx_dma_channel < 0) {
        return false;
    }

    span = ring_buffer_read_span(&driver->tx_ring);
    if (span.length == 0u) {
        dma_channel_unclaim((uint)driver->tx_dma_channel);
        driver->tx_dma_channel = -1;
        return false;
    }

    transfer_length = (span.length <= max_transfer_bytes) ? span.length : max_transfer_bytes;

    tx_dma_config = dma_channel_get_default_config((uint)driver->tx_dma_channel);
    channel_config_set_transfer_data_size(&tx_dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_dma_config, true);
    channel_config_set_write_increment(&tx_dma_config, false);
    channel_config_set_dreq(&tx_dma_config,
                            pio_get_dreq(driver->config.pio,
                                         driver->config.tx_state_machine,
                                         true));
    dma_channel_configure((uint)driver->tx_dma_channel,
                          &tx_dma_config,
                          &driver->config.pio->txf[driver->config.tx_state_machine],
                          span.data,
                          (uint32_t)transfer_length,
                          true);

    driver->tx_dma_bytes_in_flight = transfer_length;
    driver->tx_dma_active = true;
    return true;
}

static void pio_uart_driver_poll_tx_dma(pio_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->tx_dma_active) {
        return;
    }

    if (dma_channel_is_busy((uint)driver->tx_dma_channel)) {
        return;
    }

    (void)ring_buffer_commit_consumed(&driver->tx_ring, driver->tx_dma_bytes_in_flight);
    driver->tx_dma_bytes += driver->tx_dma_bytes_in_flight;
    driver->tx_dma_bytes_in_flight = 0u;
    driver->tx_dma_active = false;
    dma_channel_unclaim((uint)driver->tx_dma_channel);
    driver->tx_dma_channel = -1;
}

static void pio_uart_driver_service_tx(pio_uart_driver_t *driver)
{
    size_t occupancy;
    size_t threshold;
    size_t max_transfer_bytes = PIO_UART_DRIVER_DEFAULT_TX_DMA_MAX_TRANSFER_BYTES;

    if (driver == NULL) {
        return;
    }

    if (driver->tx_dma_active) {
        pio_uart_driver_poll_tx_dma(driver);
        if (driver->tx_dma_active) {
            return;
        }
    }

    occupancy = ring_buffer_occupancy(&driver->tx_ring);
    threshold = pio_uart_driver_dma_threshold(driver);

    if (occupancy >= threshold) {
        if (max_transfer_bytes > occupancy) {
            max_transfer_bytes = occupancy;
        }

        (void)pio_uart_driver_start_tx_dma(driver, max_transfer_bytes);
        if (driver->tx_dma_active) {
            return;
        }
    }

    pio_uart_driver_drain_tx_fifo(driver);
}

static void pio_uart_driver_harvest_framing_errors(pio_uart_driver_t *driver)
{
    /* irq set 0 rel raises the flag whose index matches the RX state machine. */
    if (pio_interrupt_get(driver->config.pio, driver->config.rx_state_machine)) {
        pio_interrupt_clear(driver->config.pio, driver->config.rx_state_machine);
        driver->rx_error_count += 1u;
    }
}

bool pio_uart_driver_init(pio_uart_driver_t *driver)
{
    if ((driver == NULL) || (driver->config.pio == NULL)) {
        return false;
    }

    if ((driver->config.tx_pin == PIO_UART_DRIVER_PIN_UNASSIGNED) ||
        (driver->config.rx_pin == PIO_UART_DRIVER_PIN_UNASSIGNED)) {
        return false;
    }

    if (driver->config.tx_state_machine == driver->config.rx_state_machine) {
        return false;
    }

    if ((driver->config.tx_state_machine >= 4u) || (driver->config.rx_state_machine >= 4u)) {
        return false;
    }

    if (!pio_uart_driver_baud_rate_supported(driver->config.baud_rate)) {
        return false;
    }

    driver->rx_dma_channel = -1;
    driver->tx_dma_channel = -1;
    driver->tx_dma_active = false;
    driver->tx_dma_bytes_in_flight = 0u;
    driver->tx_polled_bytes = 0u;
    driver->tx_dma_bytes = 0u;
    driver->controller_rx_bytes = 0u;
    driver->rx_error_count = 0u;
    driver->rx_dma_last_progress = 0u;

    if (!ring_buffer_init(&driver->rx_ring, driver->rx_storage, sizeof(driver->rx_storage))) {
        return false;
    }

    if (!ring_buffer_init(&driver->tx_ring, driver->tx_storage, sizeof(driver->tx_storage))) {
        return false;
    }

    driver->rx_dma_channel = dma_claim_unused_channel(false);
    if (driver->rx_dma_channel < 0) {
        return false;
    }

    pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, false);
    pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, false);
    pio_sm_clear_fifos(driver->config.pio, driver->config.tx_state_machine);
    pio_sm_clear_fifos(driver->config.pio, driver->config.rx_state_machine);
    pio_sm_restart(driver->config.pio, driver->config.tx_state_machine);
    pio_sm_restart(driver->config.pio, driver->config.rx_state_machine);

    pio_uart_driver_init_tx_sm(driver);
    pio_uart_driver_init_rx_sm(driver);
    pio_uart_driver_start_rx_dma(driver);
    driver->initialized = true;
    return true;
}

void pio_uart_driver_poll(pio_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->initialized) {
        return;
    }

    pio_uart_driver_publish_rx(driver);
    pio_uart_driver_harvest_framing_errors(driver);
    /* Safety net if the DMA IRQ was masked or delayed past transfer completion. */
    if ((driver->rx_dma_channel >= 0) &&
        !dma_channel_is_busy((uint)driver->rx_dma_channel) &&
        (uart_dma_rx_transfer_count_remaining((uint)driver->rx_dma_channel) == 0u)) {
        uint32_t interrupt_status = save_and_disable_interrupts();

        if (!dma_channel_is_busy((uint)driver->rx_dma_channel) &&
            (uart_dma_rx_transfer_count_remaining((uint)driver->rx_dma_channel) == 0u)) {
            pio_uart_driver_rearm_rx_dma(driver);
        }
        restore_interrupts(interrupt_status);
    }

    if (!driver->tx_dma_active && (ring_buffer_occupancy(&driver->tx_ring) == 0u)) {
        return;
    }

    pio_uart_driver_service_tx(driver);
}

void pio_uart_driver_deinit(pio_uart_driver_t *driver)
{
    if (driver == NULL) {
        return;
    }

    if (driver->initialized) {
        pio_uart_driver_publish_rx(driver);
        pio_uart_driver_release_dma(driver);
        pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, false);
        pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, false);
    }

    driver->initialized = false;
}

static bool pio_uart_driver_rx_line_idle(const pio_uart_driver_t *driver)
{
    if ((driver->config.pin_flags & PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH) == 0u) {
        return true;
    }

    return gpio_get(driver->config.rx_pin);
}

static bool pio_uart_driver_prepare_baud_change_locked(pio_uart_driver_t *driver)
{
    pio_uart_driver_publish_rx(driver);
    pio_uart_driver_poll_tx_dma(driver);

    if (driver->tx_dma_active ||
        (ring_buffer_occupancy(&driver->tx_ring) != 0u) ||
        !pio_sm_is_tx_fifo_empty(driver->config.pio, driver->config.tx_state_machine) ||
        !pio_sm_is_rx_fifo_empty(driver->config.pio, driver->config.rx_state_machine) ||
        !pio_uart_driver_rx_line_idle(driver)) {
        /* Continuous traffic defers the change; uart_driver applies a bounded timeout. */
        return false;
    }

    {
        uint32_t interrupt_status = save_and_disable_interrupts();

        if (driver->rx_dma_channel >= 0) {
            dma_irqn_set_channel_enabled(PIO_UART_DRIVER_RX_DMA_IRQ_INDEX,
                                         (uint)driver->rx_dma_channel,
                                         false);
            pio_uart_driver_abort_dma_channel((uint)driver->rx_dma_channel);
            dma_irqn_acknowledge_channel(PIO_UART_DRIVER_RX_DMA_IRQ_INDEX,
                                         (uint)driver->rx_dma_channel);
            pio_uart_driver_publish_rx(driver);
        }
        restore_interrupts(interrupt_status);
    }

    pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, false);
    pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, false);

    if (!pio_sm_is_rx_fifo_empty(driver->config.pio, driver->config.rx_state_machine) ||
        !pio_sm_is_tx_fifo_empty(driver->config.pio, driver->config.tx_state_machine) ||
        !pio_uart_driver_rx_line_idle(driver)) {
        pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, true);
        pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, true);
        if (driver->rx_dma_channel >= 0) {
            pio_uart_driver_start_rx_dma(driver);
        }
        return false;
    }

    return true;
}

static void pio_uart_driver_apply_baud_locked(pio_uart_driver_t *driver, uint32_t baud_rate)
{
    float divider = pio_uart_driver_clock_divider(baud_rate);

    driver->config.baud_rate = baud_rate;
    pio_sm_set_clkdiv(driver->config.pio, driver->config.tx_state_machine, divider);
    pio_sm_set_clkdiv(driver->config.pio, driver->config.rx_state_machine, divider);
    pio_sm_clear_fifos(driver->config.pio, driver->config.tx_state_machine);
    pio_sm_clear_fifos(driver->config.pio, driver->config.rx_state_machine);
    pio_sm_restart(driver->config.pio, driver->config.tx_state_machine);
    pio_sm_restart(driver->config.pio, driver->config.rx_state_machine);
    pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, true);
    pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, true);
    if (driver->rx_dma_channel >= 0) {
        pio_uart_driver_start_rx_dma(driver);
    }
}

bool pio_uart_driver_set_baud_rate(pio_uart_driver_t *driver, uint32_t baud_rate)
{
    if ((driver == NULL) || !driver->initialized || !pio_uart_driver_baud_rate_supported(baud_rate)) {
        return false;
    }

    /* Same-core prepare/apply; DMA IRQ1 re-arm stays enabled for sibling PIO ports. */
    if (!pio_uart_driver_prepare_baud_change_locked(driver)) {
        return false;
    }

    pio_uart_driver_apply_baud_locked(driver, baud_rate);
    driver->tx_dma_bytes_in_flight = 0u;
    driver->tx_dma_active = false;
    pio_uart_driver_poll(driver);
    return true;
}
