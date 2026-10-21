/**
 * @file hw_uart_backend.c
 * @brief Native HW UART backend policy and operations table.
 */

#include "uart/backend/backend.h"
#include "uart/hw/hw_uart_driver.h"
#include "uart/hw/baud_rate.h"
#include "uart/line_coding.h"
#include "hardware/clocks.h"

/** @brief Translate validated common parity into the SDK encoding. */
static uart_parity_t hw_uart_backend_parity(uart_driver_parity_t parity)
{
    return parity == UART_DRIVER_PARITY_ODD ? UART_PARITY_ODD
           : parity == UART_DRIVER_PARITY_EVEN ? UART_PARITY_EVEN : UART_PARITY_NONE;
}

bool hw_uart_driver_load_port_config(void *context, const uart_driver_port_info_t *port_info)
{
    hw_uart_driver_t *driver = context;
    if ((driver == NULL) || driver->initialized || (port_info == NULL) ||
        ((unsigned)port_info->id >= UART_PORT_COUNT) || (port_info->backend != UART_DRIVER_BACKEND_HW) ||
        (hw_uart_driver_configs[port_info->id].port != port_info)) {
        return false;
    }
    driver->config = &hw_uart_driver_configs[port_info->id];
    driver->baud_rate = port_info->baud_rate;
    driver->data_bits = port_info->data_bits;
    driver->stop_bits = port_info->stop_bits;
    driver->parity = hw_uart_backend_parity(port_info->parity);
    return true;
}

bool hw_uart_driver_set_line_coding(void *context, const uart_driver_line_coding_t *line_coding)
{
    const hw_uart_driver_t *driver = context;
    uint32_t actual_rate;
    if ((driver == NULL) || !driver->initialized || !uart_line_coding_is_valid(line_coding) ||
        !hw_uart_baud_rate_supported(line_coding->baud_rate, clock_get_hz(clk_peri), &actual_rate)) {
        return false;
    }
    return hw_uart_driver_apply_line_format(context, line_coding->baud_rate, line_coding->data_bits,
                                            line_coding->stop_bits, hw_uart_backend_parity(line_coding->parity));
}

static bool hw_uart_driver_is_initialized(const void *context)
{
    const hw_uart_driver_t *driver = context;
    return (driver != NULL) && driver->initialized;
}

static ring_buffer_t *hw_uart_driver_rx_ring(void *context)
{
    hw_uart_driver_t *driver = context;
    return driver != NULL ? &driver->rx_ring : NULL;
}

static ring_buffer_t *hw_uart_driver_tx_ring(void *context)
{
    hw_uart_driver_t *driver = context;
    return driver != NULL ? &driver->tx_ring : NULL;
}

/** @brief Match common line coding against applied hardware-divider and SDK framing values. */
static bool hw_uart_driver_line_coding_matches(const void *context, const uart_driver_line_coding_t *line_coding)
{
    const hw_uart_driver_t *driver = context;
    uint32_t actual_rate;
    return (driver != NULL) && driver->initialized && uart_line_coding_is_valid(line_coding) &&
           hw_uart_baud_rate_supported(line_coding->baud_rate, clock_get_hz(clk_peri), &actual_rate) &&
           (driver->baud_rate == actual_rate) && (driver->data_bits == line_coding->data_bits) &&
           (driver->stop_bits == line_coding->stop_bits) &&
           (driver->parity == hw_uart_backend_parity(line_coding->parity));
}

/** @brief Reject malformed requests before checking hardware-divider feasibility. */
static bool hw_uart_driver_line_coding_acceptable(const uart_driver_line_coding_t *line_coding)
{
    uint32_t actual_rate;
    return uart_line_coding_is_valid(line_coding) &&
           hw_uart_baud_rate_supported(line_coding->baud_rate, clock_get_hz(clk_peri), &actual_rate);
}

static uint32_t hw_uart_driver_baud_rate(const void *context)
{
    const hw_uart_driver_t *driver = context;
    return driver != NULL ? driver->baud_rate : 0u;
}

/** @brief Report hardware-completed bytes and receive errors. */
static uart_backend_stats_t hw_uart_driver_stats(const void *context)
{
    const hw_uart_driver_t *driver = context;
    if (driver == NULL) {
        return (uart_backend_stats_t){0};
    }
    return (uart_backend_stats_t){
        .controller_tx_bytes = driver->controller_tx_bytes,
        .controller_rx_bytes = driver->controller_rx_bytes,
        .rx_error_count = driver->rx_error_count,
    };
}

const uart_backend_ops_t uart_backend_hw_ops = {
    .load_config = hw_uart_driver_load_port_config,
    .is_initialized = hw_uart_driver_is_initialized,
    .init = hw_uart_driver_init,
    .deinit = hw_uart_driver_deinit,
    .poll = hw_uart_driver_poll,
    .rx_ring = hw_uart_driver_rx_ring,
    .tx_ring = hw_uart_driver_tx_ring,
    .line_coding_matches = hw_uart_driver_line_coding_matches,
    .line_coding_acceptable = hw_uart_driver_line_coding_acceptable,
    .set_line_coding = hw_uart_driver_set_line_coding,
    .rx_snapshot_is_current = hw_uart_driver_rx_snapshot_is_current,
    .clear_rx_error_baseline = hw_uart_driver_clear_rx_error_baseline,
    .baud_rate = hw_uart_driver_baud_rate,
    .stats = hw_uart_driver_stats,
};