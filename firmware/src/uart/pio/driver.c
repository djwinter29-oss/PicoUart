/**
 * @file driver.c
 * @brief PIO UART backend for PicoUart logical UART ports.
 */

#include "uart/pio/internal.h"

#include "uart.pio.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

/** @brief Minimum clock divider supported by the Pico SDK helper. */
#define PIO_UART_DRIVER_MIN_CLOCK_DIVIDER 1.0f
/** @brief Maximum clock divider supported by the Pico SDK helper. */
#define PIO_UART_DRIVER_MAX_CLOCK_DIVIDER 65536.0f
/** @brief Joined PIO TX FIFO depth in 32-bit entries. */
#define PIO_UART_DRIVER_TX_FIFO_DEPTH 8u
/** @brief Maximum bytes launched in one TX DMA transfer. */
#define PIO_UART_DRIVER_DEFAULT_TX_DMA_MAX_TRANSFER_BYTES 256u

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

static void pio_uart_driver_fill_rx_ring(pio_uart_driver_t *driver);
static bool pio_uart_driver_prepare_baud_change_locked(pio_uart_driver_t *driver);
static void pio_uart_driver_apply_baud_locked(pio_uart_driver_t *driver, uint32_t baud_rate);
static void pio_uart_driver_release_dma(pio_uart_driver_t *driver);
static bool pio_uart_driver_start_tx_dma(pio_uart_driver_t *driver, size_t max_transfer_bytes);
static void pio_uart_driver_poll_tx_dma(pio_uart_driver_t *driver);

static size_t pio_uart_driver_dma_threshold(const pio_uart_driver_t *driver)
{
    if ((driver != NULL) && (driver->config.tx_dma_start_threshold != 0u)) {
        return (size_t)driver->config.tx_dma_start_threshold;
    }
    return PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD;
}

static void pio_uart_driver_release_dma(pio_uart_driver_t *driver)
{
    if ((driver != NULL) && (driver->tx_dma_channel >= 0)) {
        dma_channel_abort((uint)driver->tx_dma_channel);
        dma_channel_unclaim((uint)driver->tx_dma_channel);
        driver->tx_dma_channel = -1;
    }

    if (driver != NULL) {
        driver->tx_dma_bytes_in_flight = 0u;
        driver->tx_dma_active = false;
    }
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
    float divider;

    if (baud_rate == 0u) {
        return false;
    }

    divider = pio_uart_driver_clock_divider(baud_rate);
    return (divider >= PIO_UART_DRIVER_MIN_CLOCK_DIVIDER) &&
           (divider < PIO_UART_DRIVER_MAX_CLOCK_DIVIDER);
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

static void pio_uart_driver_fill_rx_ring(pio_uart_driver_t *driver)
{
    pio_uart_driver_harvest_framing_errors(driver);

    while (!pio_sm_is_rx_fifo_empty(driver->config.pio, driver->config.rx_state_machine)) {
        ring_buffer_span_t span = ring_buffer_write_span(&driver->rx_ring);
        size_t produced = 0u;

        if (span.length == 0u) {
            while (!pio_sm_is_rx_fifo_empty(driver->config.pio, driver->config.rx_state_machine)) {
                uint32_t word = pio_sm_get(driver->config.pio, driver->config.rx_state_machine);
                uint8_t byte = (uint8_t)(word >> 24);

                driver->controller_rx_bytes += 1u;
                ring_buffer_write_byte_overwrite(&driver->rx_ring, byte);
            }

            continue;
        }

        while ((produced < span.length) &&
               !pio_sm_is_rx_fifo_empty(driver->config.pio, driver->config.rx_state_machine)) {
            uint32_t word = pio_sm_get(driver->config.pio, driver->config.rx_state_machine);
            uint8_t byte = (uint8_t)(word >> 24);

            span.data[produced] = byte;
            produced += 1u;
            driver->controller_rx_bytes += 1u;
        }

        if (produced != 0u) {
            (void)ring_buffer_commit_produced(&driver->rx_ring, produced);
        }
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

    driver->tx_dma_channel = -1;
    driver->tx_dma_active = false;
    driver->tx_dma_bytes_in_flight = 0u;
    driver->tx_polled_bytes = 0u;
    driver->tx_dma_bytes = 0u;
    driver->controller_rx_bytes = 0u;
    driver->rx_error_count = 0u;

    if (!ring_buffer_init(&driver->rx_ring, driver->rx_storage, sizeof(driver->rx_storage))) {
        return false;
    }

    if (!ring_buffer_init(&driver->tx_ring, driver->tx_storage, sizeof(driver->tx_storage))) {
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
    driver->initialized = true;
    return true;
}

void pio_uart_driver_poll(pio_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->initialized) {
        return;
    }

    pio_uart_driver_fill_rx_ring(driver);

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
    pio_uart_driver_fill_rx_ring(driver);
    pio_uart_driver_poll_tx_dma(driver);

    if (driver->tx_dma_active ||
        (ring_buffer_occupancy(&driver->tx_ring) != 0u) ||
        !pio_sm_is_tx_fifo_empty(driver->config.pio, driver->config.tx_state_machine) ||
        !pio_sm_is_rx_fifo_empty(driver->config.pio, driver->config.rx_state_machine) ||
        !pio_uart_driver_rx_line_idle(driver)) {
        /* Continuous traffic defers the change; uart_driver applies a bounded timeout. */
        return false;
    }

    pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, false);
    pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, false);

    if (!pio_sm_is_rx_fifo_empty(driver->config.pio, driver->config.rx_state_machine) ||
        !pio_sm_is_tx_fifo_empty(driver->config.pio, driver->config.tx_state_machine) ||
        !pio_uart_driver_rx_line_idle(driver)) {
        pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, true);
        pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, true);
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
}

bool pio_uart_driver_set_baud_rate(pio_uart_driver_t *driver, uint32_t baud_rate)
{
    if ((driver == NULL) || !driver->initialized || !pio_uart_driver_baud_rate_supported(baud_rate)) {
        return false;
    }

    /*
     * Mask only DMA IRQ0 (HW UART RX re-arm) instead of disabling all IRQs, so a
     * concurrent HW UART flood does not widen its FIFO overrun window.
     */
    irq_set_enabled(DMA_IRQ_0, false);
    if (!pio_uart_driver_prepare_baud_change_locked(driver)) {
        irq_set_enabled(DMA_IRQ_0, true);
        return false;
    }

    pio_uart_driver_apply_baud_locked(driver, baud_rate);
    driver->tx_dma_bytes_in_flight = 0u;
    driver->tx_dma_active = false;
    irq_set_enabled(DMA_IRQ_0, true);
    pio_uart_driver_poll(driver);
    return true;
}
