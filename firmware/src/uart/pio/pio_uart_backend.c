/**
 * @file pio_uart_backend.c
 * @brief Native PIO UART backend policy and operations table.
 */

#include "uart/backend/backend.h"
#include "uart/pio/pio_uart_driver_internal.h"
#include "uart/line_coding.h"
#include "hardware/clocks.h"

bool pio_uart_driver_load_port_config(void *context, const uart_driver_port_info_t *port_info)
{
    pio_uart_driver_t *driver = context;
    if ((driver == NULL) || driver->initialized || (port_info == NULL) ||
        ((unsigned)port_info->id >= UART_PORT_COUNT) || (port_info->backend != UART_DRIVER_BACKEND_PIO) ||
        (pio_uart_driver_configs[port_info->id].port != port_info)) {
        return false;
    }
    driver->config = &pio_uart_driver_configs[port_info->id];
    driver->baud_rate = port_info->baud_rate;
    return true;
}

bool pio_uart_driver_set_line_coding(void *context, const uart_driver_line_coding_t *line_coding)
{
    const pio_uart_driver_t *driver = context;
    if ((driver == NULL) || !driver->initialized || !uart_line_coding_is_valid(line_coding) ||
        !uart_line_coding_pio_supported(line_coding, clock_get_hz(clk_sys))) {
        return false;
    }
    return pio_uart_driver_apply_baud_rate(context, line_coding->baud_rate);
}

static bool pio_uart_driver_is_initialized(const void *context)
{
    const pio_uart_driver_t *driver = context;
    return (driver != NULL) && driver->initialized;
}

static ring_buffer_t *pio_uart_driver_rx_ring(void *context)
{
    pio_uart_driver_t *driver = context;
    return driver != NULL ? &driver->rx_ring : NULL;
}

static ring_buffer_t *pio_uart_driver_tx_ring(void *context)
{
    pio_uart_driver_t *driver = context;
    return driver != NULL ? &driver->tx_ring : NULL;
}

/** @brief Match an 8N1 request against the applied PIO rate. */
static bool pio_uart_driver_line_coding_matches(const void *context, const uart_driver_line_coding_t *line_coding)
{
    const pio_uart_driver_t *driver = context;
        return (driver != NULL) && driver->initialized && uart_line_coding_is_valid(line_coding) &&
            uart_line_coding_pio_supported(line_coding, clock_get_hz(clk_sys)) &&
            (driver->baud_rate == line_coding->baud_rate);
}

/** @brief Reject non-8N1 or unrepresentable TX/RX clock-divider requests. */
static bool pio_uart_driver_line_coding_acceptable(const uart_driver_line_coding_t *line_coding)
{
        return uart_line_coding_is_valid(line_coding) &&
            uart_line_coding_pio_supported(line_coding, clock_get_hz(clk_sys));
}

static uint32_t pio_uart_driver_baud_rate(const void *context)
{
    const pio_uart_driver_t *driver = context;
    return driver != NULL ? driver->baud_rate : 0u;
}

/** @brief Combine polled and DMA-completed TX counts without including queued bytes. */
static uart_backend_stats_t pio_uart_driver_stats(const void *context)
{
    const pio_uart_driver_t *driver = context;
    if (driver == NULL) {
        return (uart_backend_stats_t){0};
    }
    return (uart_backend_stats_t){
        .controller_tx_bytes = (uint32_t)(driver->tx_polled_bytes + driver->tx_dma_bytes),
        .controller_rx_bytes = driver->controller_rx_bytes,
        .rx_error_count = driver->rx_error_count,
    };
}

const uart_backend_ops_t uart_backend_pio_ops = {
    .load_config = pio_uart_driver_load_port_config,
    .is_initialized = pio_uart_driver_is_initialized,
    .init = pio_uart_driver_init,
    .deinit = pio_uart_driver_deinit,
    .poll = pio_uart_driver_poll,
    .rx_ring = pio_uart_driver_rx_ring,
    .tx_ring = pio_uart_driver_tx_ring,
    .line_coding_matches = pio_uart_driver_line_coding_matches,
    .line_coding_acceptable = pio_uart_driver_line_coding_acceptable,
    .set_line_coding = pio_uart_driver_set_line_coding,
    .rx_snapshot_is_current = pio_uart_driver_rx_snapshot_is_current,
    .clear_rx_error_baseline = pio_uart_driver_clear_rx_error_baseline,
    .baud_rate = pio_uart_driver_baud_rate,
    .stats = pio_uart_driver_stats,
};