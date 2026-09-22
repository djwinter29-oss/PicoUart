/**
 * @file adapter.c
 * @brief Adapters from the common UART backend contract to concrete drivers.
 */

#include "uart/backend/adapter.h"

#include "hardware/clocks.h"
#include "uart/hw/baud_rate.h"
#include "uart/hw/hw_uart_driver.h"
#include "uart/line_coding.h"
#include "uart/pio/pio_uart_driver_internal.h"

static uart_parity_t uart_backend_hw_parity(uart_driver_parity_t parity)
{
    if (parity == UART_DRIVER_PARITY_ODD) {
        return UART_PARITY_ODD;
    }

    if (parity == UART_DRIVER_PARITY_EVEN) {
        return UART_PARITY_EVEN;
    }

    return UART_PARITY_NONE;
}

static bool uart_backend_hw_is_initialized(const uart_backend_instance_t *instance)
{
    return instance->hw.initialized;
}

static bool uart_backend_hw_init(uart_backend_instance_t *instance)
{
    return hw_uart_driver_init(&instance->hw);
}

static void uart_backend_hw_deinit(uart_backend_instance_t *instance)
{
    hw_uart_driver_deinit(&instance->hw);
}

static void uart_backend_hw_poll(uart_backend_instance_t *instance, bool tx_launch_allowed)
{
    hw_uart_driver_poll(&instance->hw, tx_launch_allowed);
}

static ring_buffer_t *uart_backend_hw_rx_ring(uart_backend_instance_t *instance)
{
    return &instance->hw.rx_ring;
}

static ring_buffer_t *uart_backend_hw_tx_ring(uart_backend_instance_t *instance)
{
    return &instance->hw.tx_ring;
}

static bool uart_backend_hw_line_coding_matches(const uart_backend_instance_t *instance,
                                                 const uart_driver_line_coding_t *line_coding)
{
    const hw_uart_driver_t *driver = &instance->hw;
    uint32_t actual_rate;

    return hw_uart_baud_rate_supported(line_coding->baud_rate,
                                       clock_get_hz(clk_peri),
                                       &actual_rate) &&
           (driver->config.baud_rate == actual_rate) &&
           (driver->config.data_bits == line_coding->data_bits) &&
           (driver->config.stop_bits == line_coding->stop_bits) &&
           (driver->config.parity == uart_backend_hw_parity(line_coding->parity));
}

static bool uart_backend_hw_line_coding_acceptable(const uart_driver_line_coding_t *line_coding)
{
    uint32_t actual_rate;

    return hw_uart_baud_rate_supported(line_coding->baud_rate,
                                       clock_get_hz(clk_peri),
                                       &actual_rate);
}

static bool uart_backend_hw_set_line_coding(uart_backend_instance_t *instance,
                                             const uart_driver_line_coding_t *line_coding)
{
    return hw_uart_driver_set_line_format(&instance->hw,
                                          line_coding->baud_rate,
                                          line_coding->data_bits,
                                          line_coding->stop_bits,
                                          uart_backend_hw_parity(line_coding->parity));
}

static bool uart_backend_hw_rx_snapshot_is_current(const uart_backend_instance_t *instance,
                                                    uint32_t consumer_sequence)
{
    return hw_uart_driver_rx_snapshot_is_current(&instance->hw, consumer_sequence);
}

static void uart_backend_hw_clear_rx_error_baseline(uart_backend_instance_t *instance)
{
    hw_uart_driver_clear_rx_error_baseline(&instance->hw);
}

static uint32_t uart_backend_hw_baud_rate(const uart_backend_instance_t *instance)
{
    return instance->hw.config.baud_rate;
}

static uart_backend_stats_t uart_backend_hw_stats(const uart_backend_instance_t *instance)
{
    const hw_uart_driver_t *driver = &instance->hw;

    return (uart_backend_stats_t){
        .controller_tx_bytes = driver->controller_tx_bytes,
        .controller_rx_bytes = driver->controller_rx_bytes,
        .rx_error_count = driver->rx_error_count,
    };
}

static bool uart_backend_pio_is_initialized(const uart_backend_instance_t *instance)
{
    return instance->pio.initialized;
}

static bool uart_backend_pio_init(uart_backend_instance_t *instance)
{
    return pio_uart_driver_init(&instance->pio);
}

static void uart_backend_pio_deinit(uart_backend_instance_t *instance)
{
    pio_uart_driver_deinit(&instance->pio);
}

static void uart_backend_pio_poll(uart_backend_instance_t *instance, bool tx_launch_allowed)
{
    pio_uart_driver_poll(&instance->pio, tx_launch_allowed);
}

static ring_buffer_t *uart_backend_pio_rx_ring(uart_backend_instance_t *instance)
{
    return &instance->pio.rx_ring;
}

static ring_buffer_t *uart_backend_pio_tx_ring(uart_backend_instance_t *instance)
{
    return &instance->pio.tx_ring;
}

static bool uart_backend_pio_line_coding_matches(const uart_backend_instance_t *instance,
                                                  const uart_driver_line_coding_t *line_coding)
{
    const pio_uart_driver_t *driver = &instance->pio;

    return (driver->config.baud_rate == line_coding->baud_rate) &&
           (line_coding->data_bits == 8u) &&
           (line_coding->stop_bits == 1u) &&
           (line_coding->parity == UART_DRIVER_PARITY_NONE);
}

static bool uart_backend_pio_line_coding_acceptable(const uart_driver_line_coding_t *line_coding)
{
    return uart_line_coding_pio_supported(line_coding, clock_get_hz(clk_sys));
}

static bool uart_backend_pio_set_line_coding(uart_backend_instance_t *instance,
                                              const uart_driver_line_coding_t *line_coding)
{
    return pio_uart_driver_set_baud_rate(&instance->pio, line_coding->baud_rate);
}

static bool uart_backend_pio_rx_snapshot_is_current(const uart_backend_instance_t *instance,
                                                     uint32_t consumer_sequence)
{
    return pio_uart_driver_rx_snapshot_is_current(&instance->pio, consumer_sequence);
}

static void uart_backend_pio_clear_rx_error_baseline(uart_backend_instance_t *instance)
{
    (void)instance;
}

static uint32_t uart_backend_pio_baud_rate(const uart_backend_instance_t *instance)
{
    return instance->pio.config.baud_rate;
}

static uart_backend_stats_t uart_backend_pio_stats(const uart_backend_instance_t *instance)
{
    const pio_uart_driver_t *driver = &instance->pio;

    return (uart_backend_stats_t){
        .controller_tx_bytes = (uint32_t)(driver->tx_polled_bytes + driver->tx_dma_bytes),
        .controller_rx_bytes = driver->controller_rx_bytes,
        .rx_error_count = driver->rx_error_count,
    };
}

static const uart_backend_ops_t uart_backend_hw_ops = {
    .is_initialized = uart_backend_hw_is_initialized,
    .init = uart_backend_hw_init,
    .deinit = uart_backend_hw_deinit,
    .poll = uart_backend_hw_poll,
    .rx_ring = uart_backend_hw_rx_ring,
    .tx_ring = uart_backend_hw_tx_ring,
    .line_coding_matches = uart_backend_hw_line_coding_matches,
    .line_coding_acceptable = uart_backend_hw_line_coding_acceptable,
    .set_line_coding = uart_backend_hw_set_line_coding,
    .rx_snapshot_is_current = uart_backend_hw_rx_snapshot_is_current,
    .clear_rx_error_baseline = uart_backend_hw_clear_rx_error_baseline,
    .baud_rate = uart_backend_hw_baud_rate,
    .stats = uart_backend_hw_stats,
};

static const uart_backend_ops_t uart_backend_pio_ops = {
    .is_initialized = uart_backend_pio_is_initialized,
    .init = uart_backend_pio_init,
    .deinit = uart_backend_pio_deinit,
    .poll = uart_backend_pio_poll,
    .rx_ring = uart_backend_pio_rx_ring,
    .tx_ring = uart_backend_pio_tx_ring,
    .line_coding_matches = uart_backend_pio_line_coding_matches,
    .line_coding_acceptable = uart_backend_pio_line_coding_acceptable,
    .set_line_coding = uart_backend_pio_set_line_coding,
    .rx_snapshot_is_current = uart_backend_pio_rx_snapshot_is_current,
    .clear_rx_error_baseline = uart_backend_pio_clear_rx_error_baseline,
    .baud_rate = uart_backend_pio_baud_rate,
    .stats = uart_backend_pio_stats,
};

const uart_backend_ops_t *uart_backend_ops_for_type(uart_driver_backend_t backend)
{
    if (backend == UART_DRIVER_BACKEND_HW) {
        return uart_backend_ops_is_complete(&uart_backend_hw_ops) ? &uart_backend_hw_ops : NULL;
    }

    if (backend == UART_DRIVER_BACKEND_PIO) {
        return uart_backend_ops_is_complete(&uart_backend_pio_ops) ? &uart_backend_pio_ops : NULL;
    }

    return NULL;
}

void uart_backend_enable_rx_dma_irq(void)
{
    hw_uart_driver_enable_rx_dma_irq();
    pio_uart_driver_enable_rx_dma_irq();
}