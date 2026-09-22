/**
 * @file port_api.c
 * @brief Backend-independent public logical-port operations.
 */

#include "uart/port_api.h"

#include "hardware/sync.h"
#include "uart/bridge.h"
#include "uart/control/ownership.h"
#include "uart/ring_buffer/ring_buffer.h"

/** @brief Maximum attempts to acquire a coherent worker-owned telemetry snapshot. */
#define UART_PORT_API_STATS_SNAPSHOT_ATTEMPTS 3u

static uart_runtime_port_t *uart_port_api_mutable(const uart_port_api_t *api,
                                                  uart_port_id_t port_id)
{
    if ((api == NULL) || (api->ports == NULL) || (port_id >= UART_PORT_COUNT)) {
        return NULL;
    }

    return &api->ports[port_id];
}

static ring_buffer_t *uart_port_api_rx_ring(uart_runtime_port_t *port)
{
    return ((port != NULL) && (port->ops != NULL)) ? port->ops->rx_ring(&port->backend) : NULL;
}

static ring_buffer_t *uart_port_api_tx_ring(uart_runtime_port_t *port)
{
    return ((port != NULL) && (port->ops != NULL)) ? port->ops->tx_ring(&port->backend) : NULL;
}

static bool uart_port_api_snapshot_is_current(void *context, uint32_t consumer_sequence)
{
    uart_runtime_port_t *port = context;

    return (port != NULL) && (port->ops != NULL) &&
           port->ops->rx_snapshot_is_current(&port->backend, consumer_sequence);
}

size_t uart_port_api_count(void)
{
    return UART_PORT_COUNT;
}

bool uart_port_api_is_ready(const uart_port_api_t *api, uart_port_id_t port_id)
{
    uart_runtime_port_t *port = uart_port_api_mutable(api, port_id);

    return (port != NULL) && (port->ops != NULL) && port->ops->is_initialized(&port->backend);
}

size_t uart_port_api_drain_rx(const uart_port_api_t *api,
                              uart_port_id_t port_id,
                              size_t capacity,
                              uint32_t (*writer)(void *context,
                                                 const uint8_t *data,
                                                 uint32_t length),
                              void *context)
{
    uart_runtime_port_t *port = uart_port_api_mutable(api, port_id);
    ring_buffer_t *rx_ring = uart_port_api_rx_ring(port);

    if ((rx_ring == NULL) || !uart_port_api_is_ready(api, port_id) ||
        (writer == NULL) || (api->stats_sequence == NULL)) {
        return 0u;
    }

    return uart_bridge_drain_rx(rx_ring,
                                capacity,
                                writer,
                                context,
                                uart_port_api_snapshot_is_current,
                                port,
                                &api->stats_sequence[port_id]);
}

size_t uart_port_api_recover_rx(const uart_port_api_t *api, uart_port_id_t port_id)
{
    uart_runtime_port_t *port = uart_port_api_mutable(api, port_id);
    ring_buffer_t *rx_ring = uart_port_api_rx_ring(port);

    if ((rx_ring == NULL) || !uart_port_api_is_ready(api, port_id)) {
        return 0u;
    }

    return uart_bridge_recover_rx(rx_ring);
}

size_t uart_port_api_fill_tx(const uart_port_api_t *api,
                             uart_port_id_t port_id,
                             size_t capacity,
                             uint32_t (*reader)(void *context,
                                                uint8_t *data,
                                                uint32_t length),
                             void *context)
{
    uart_runtime_port_t *port = uart_port_api_mutable(api, port_id);
    ring_buffer_t *tx_ring = uart_port_api_tx_ring(port);
    uint32_t save;
    bool blocked;

    if ((tx_ring == NULL) || !uart_port_api_is_ready(api, port_id) || (reader == NULL) ||
        (api->status_flags == NULL) || (api->status_lock == NULL)) {
        return 0u;
    }

    save = spin_lock_blocking(api->status_lock);
    blocked = uart_control_tx_should_block(api->status_flags[port_id],
                                           UART_DRIVER_PORT_STATUS_CONTROL_PENDING);
    spin_unlock(api->status_lock, save);
    if (blocked) {
        return 0u;
    }

    return uart_bridge_fill_tx(tx_ring, capacity, reader, context);
}

bool uart_port_api_info(const uart_port_api_t *api,
                        uart_port_id_t port_id,
                        uart_driver_port_info_t *info)
{
    uint32_t save;
    uart_runtime_port_t *port = uart_port_api_mutable(api, port_id);

    if ((port == NULL) || (info == NULL) || (api->status_lock == NULL)) {
        return false;
    }

    save = spin_lock_blocking(api->status_lock);
    *info = port->info;
    spin_unlock(api->status_lock, save);
    return true;
}

bool uart_port_api_stats(const uart_port_api_t *api,
                         uart_port_id_t port_id,
                         uart_driver_port_stats_t *stats)
{
    uart_runtime_port_t *port = uart_port_api_mutable(api, port_id);
    ring_buffer_t *rx_ring;
    ring_buffer_t *tx_ring;
    uart_backend_stats_t backend_stats;
    uint32_t first_sequence;
    uint32_t second_sequence;

    if ((stats == NULL) || (api == NULL) || (api->stats_sequence == NULL) ||
        !uart_port_api_is_ready(api, port_id)) {
        return false;
    }

    rx_ring = uart_port_api_rx_ring(port);
    tx_ring = uart_port_api_tx_ring(port);
    if ((rx_ring == NULL) || (tx_ring == NULL)) {
        return false;
    }

    for (size_t attempt = 0u; attempt < UART_PORT_API_STATS_SNAPSHOT_ATTEMPTS; ++attempt) {
        first_sequence = api->stats_sequence[port_id];
        __dmb();
        if ((first_sequence & 1u) != 0u) {
            continue;
        }

        stats->controller_tx_bytes = 0u;
        stats->controller_rx_bytes = 0u;
        stats->tx_ring_high_watermark = (uint16_t)ring_buffer_high_watermark(tx_ring);
        stats->rx_ring_high_watermark = (uint16_t)ring_buffer_high_watermark(rx_ring);
        stats->tx_ring_overflow_count = (uint32_t)ring_buffer_overflow_count(tx_ring);
        stats->rx_ring_overflow_count = (uint32_t)ring_buffer_overflow_count(rx_ring);
        stats->rx_ring_pending_overflow_count = (uint32_t)ring_buffer_pending_overflow(rx_ring);
        stats->rx_error_count = 0u;

        backend_stats = port->ops->stats(&port->backend);
        stats->controller_tx_bytes = backend_stats.controller_tx_bytes;
        stats->controller_rx_bytes = backend_stats.controller_rx_bytes;
        stats->rx_error_count = backend_stats.rx_error_count;

        __dmb();
        second_sequence = api->stats_sequence[port_id];
        if ((first_sequence == second_sequence) && ((second_sequence & 1u) == 0u)) {
            return true;
        }
    }

    return false;
}