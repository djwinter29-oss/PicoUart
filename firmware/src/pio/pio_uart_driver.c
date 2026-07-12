/**
 * @file pio_uart_driver.c
 * @brief PIO UART backend for PicoUart logical UART ports.
 */

#include "pio/pio_uart_driver.h"

#include "pio/pio_uart.pio.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"

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

static uint pio_uart_driver_block_index(PIO pio)
{
    return (pio == pio0) ? 0u : 1u;
}

static float pio_uart_driver_clock_divider(uint32_t baud_rate)
{
    return (float)clock_get_hz(clk_sys) / (8.0f * (float)baud_rate);
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
    pio_sm_set_pins_with_mask(driver->config.pio, driver->config.tx_state_machine, 1u << driver->config.tx_pin, 1u << driver->config.tx_pin);
    pio_sm_init(driver->config.pio, driver->config.tx_state_machine, offset, &config);
    pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, true);
}

static void pio_uart_driver_init_rx_sm(pio_uart_driver_t *driver)
{
    uint offset = pio_uart_driver_rx_offset(driver->config.pio);
    pio_sm_config config = pio_uart_rx_program_get_default_config(offset);

    sm_config_set_in_pins(&config, driver->config.rx_pin);
    sm_config_set_in_shift(&config, true, false, 32u);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&config, pio_uart_driver_clock_divider(driver->config.baud_rate));

    pio_gpio_init(driver->config.pio, driver->config.rx_pin);
    gpio_pull_up(driver->config.rx_pin);
    pio_sm_set_consecutive_pindirs(driver->config.pio, driver->config.rx_state_machine, driver->config.rx_pin, 1u, false);
    pio_sm_init(driver->config.pio, driver->config.rx_state_machine, offset, &config);
    pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, true);
}

static void pio_uart_driver_drain_tx_fifo(pio_uart_driver_t *driver)
{
    while (!pio_sm_is_tx_fifo_full(driver->config.pio, driver->config.tx_state_machine)) {
        ring_buffer_span_t span = ring_buffer_read_span(&driver->tx_ring);

        if (span.length == 0u) {
            break;
        }

        pio_sm_put(driver->config.pio, driver->config.tx_state_machine, span.data[0]);
        (void)ring_buffer_commit_consumed(&driver->tx_ring, 1u);
    }
}

static void pio_uart_driver_fill_rx_ring(pio_uart_driver_t *driver)
{
    while (!pio_sm_is_rx_fifo_empty(driver->config.pio, driver->config.rx_state_machine)) {
        uint8_t byte = (uint8_t)(pio_sm_get(driver->config.pio, driver->config.rx_state_machine) >> 24);
        (void)ring_buffer_write(&driver->rx_ring, &byte, 1u);
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
    pio_uart_driver_drain_tx_fifo(driver);
}

void pio_uart_driver_deinit(pio_uart_driver_t *driver)
{
    if (driver == NULL) {
        return;
    }

    if (driver->initialized) {
        pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, false);
        pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, false);
    }

    driver->initialized = false;
}

size_t pio_uart_driver_read(pio_uart_driver_t *driver, uint8_t *data, size_t capacity)
{
    if ((driver == NULL) || (data == NULL) || !driver->initialized) {
        return 0u;
    }

    pio_uart_driver_fill_rx_ring(driver);
    return ring_buffer_read(&driver->rx_ring, data, capacity);
}

size_t pio_uart_driver_write_available(const pio_uart_driver_t *driver)
{
    if ((driver == NULL) || !driver->initialized) {
        return 0u;
    }

    return ring_buffer_free_space(&driver->tx_ring);
}

bool pio_uart_driver_set_baud_rate(pio_uart_driver_t *driver, uint32_t baud_rate)
{
    if ((driver == NULL) || !driver->initialized || (baud_rate == 0u)) {
        return false;
    }

    driver->config.baud_rate = baud_rate;
    pio_sm_set_enabled(driver->config.pio, driver->config.tx_state_machine, false);
    pio_sm_set_enabled(driver->config.pio, driver->config.rx_state_machine, false);
    pio_sm_clear_fifos(driver->config.pio, driver->config.tx_state_machine);
    pio_sm_clear_fifos(driver->config.pio, driver->config.rx_state_machine);
    pio_sm_restart(driver->config.pio, driver->config.tx_state_machine);
    pio_sm_restart(driver->config.pio, driver->config.rx_state_machine);
    pio_uart_driver_init_tx_sm(driver);
    pio_uart_driver_init_rx_sm(driver);
    pio_uart_driver_drain_tx_fifo(driver);
    return true;
}

size_t pio_uart_driver_write(pio_uart_driver_t *driver, const uint8_t *data, size_t length)
{
    size_t written;

    if ((driver == NULL) || (data == NULL) || !driver->initialized) {
        return 0u;
    }

    written = ring_buffer_write(&driver->tx_ring, data, length);
    pio_uart_driver_drain_tx_fifo(driver);

    return written;
}