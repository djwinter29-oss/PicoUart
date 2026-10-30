// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file test_uart_board_config.c
 * @brief Host tests for compile-time board references and backend-owned resources.
 */

#include "unity.h"

#include "uart/uart_driver.h"
#include "board/uart_topology.h"
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "uart/hw/hw_uart_driver.h"
#include "uart/pio/pio_uart_driver.h"

uart_inst_t uart0_test_instance;
uart_inst_t uart1_test_instance;
pio_hw_t pio0_test_instance;
pio_hw_t pio1_test_instance;

static const uint32_t expected_tx_pins[UART_PORT_COUNT] = {0u, 4u, 8u, 12u, 16u, 20u};
static const uint32_t expected_rx_pins[UART_PORT_COUNT] = {1u, 5u, 9u, 13u, 17u, 21u};
static const uint32_t expected_rts_pins[UART_PORT_COUNT] = {3u, 7u, 10u, 14u, 18u, 22u};
static const uint32_t expected_cts_pins[UART_PORT_COUNT] = {2u, 6u, 11u, 15u, 19u, 26u};
static const uart_inst_t *const expected_hw[UART_PORT_COUNT] = {uart0, uart1, NULL, NULL, NULL, NULL};
static const PIO expected_pio[UART_PORT_COUNT] = {NULL, NULL, pio0, pio0, pio1, pio1};
static const uint32_t expected_tx_state_machines[UART_PORT_COUNT] = {0u, 0u, 0u, 2u, 0u, 2u};
static const uint32_t expected_rx_state_machines[UART_PORT_COUNT] = {0u, 0u, 1u, 3u, 1u, 3u};

void setUp(void)
{
}

void tearDown(void)
{
}

void test_actual_board_table_resolves_to_unique_backend_resources(void)
{
    uart_board_topology_port_t topology[UART_PORT_COUNT] = {0};

    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        const uart_driver_port_info_t *port = &uart_board_ports[index];

        TEST_ASSERT_EQUAL_UINT(index, port->id);
        TEST_ASSERT_EQUAL_UINT32(115200u, port->baud_rate);
        TEST_ASSERT_EQUAL_UINT32(expected_tx_pins[index], port->tx_pin);
        TEST_ASSERT_EQUAL_UINT32(expected_rx_pins[index], port->rx_pin);
        TEST_ASSERT_EQUAL_UINT32(expected_rts_pins[index], port->rts_pin);
        TEST_ASSERT_EQUAL_UINT32(expected_cts_pins[index], port->cts_pin);
        TEST_ASSERT_EQUAL_UINT8(8u, port->data_bits);
        TEST_ASSERT_EQUAL_UINT8(1u, port->stop_bits);
        TEST_ASSERT_EQUAL_UINT(UART_DRIVER_PARITY_NONE, port->parity);

        topology[index] = (uart_board_topology_port_t){
            .id = port->id,
            .backend = port->backend,
            .baud_rate = port->baud_rate,
            .tx_pin = port->tx_pin,
            .rx_pin = port->rx_pin,
            .target_gpio_count = 30u,
            .rts_pin = UART_BOARD_TOPOLOGY_PIN_UNASSIGNED,
            .cts_pin = UART_BOARD_TOPOLOGY_PIN_UNASSIGNED,
        };

        if (port->backend == UART_DRIVER_BACKEND_HW) {
            const hw_uart_driver_config_t config = hw_uart_driver_configs[index];
            TEST_ASSERT_EQUAL_PTR(port, config.port);
            topology[index].backend_instance = (uintptr_t)config.instance;
            topology[index].rts_pin = config.port->rts_pin;
            topology[index].cts_pin = config.port->cts_pin;
            topology[index].rts_enabled = config.hardware_flow_control;
            topology[index].cts_enabled = config.hardware_flow_control;
            TEST_ASSERT_EQUAL_PTR(expected_hw[index], config.instance);
            TEST_ASSERT_EQUAL_UINT32(expected_rts_pins[index], config.port->rts_pin);
            TEST_ASSERT_EQUAL_UINT32(expected_cts_pins[index], config.port->cts_pin);
            TEST_ASSERT_FALSE(config.hardware_flow_control);
            TEST_ASSERT_EQUAL_UINT8(8u, config.port->data_bits);
            TEST_ASSERT_EQUAL_UINT8(1u, config.port->stop_bits);
            TEST_ASSERT_EQUAL(UART_DRIVER_PARITY_NONE, config.port->parity);
        } else {
            const pio_uart_driver_config_t config = pio_uart_driver_configs[index];
            TEST_ASSERT_EQUAL_UINT(UART_DRIVER_BACKEND_PIO, port->backend);
            TEST_ASSERT_EQUAL_PTR(port, config.port);
            topology[index].backend_instance = (uintptr_t)config.pio;
            topology[index].tx_state_machine = config.tx_state_machine;
            topology[index].rx_state_machine = config.rx_state_machine;
            topology[index].rts_pin = config.port->rts_pin;
            topology[index].cts_pin = config.port->cts_pin;
            topology[index].rts_enabled = (config.pin_flags & PIO_UART_DRIVER_PIN_FLAG_RX_FLOW_CONTROL) != 0u;
            topology[index].cts_enabled = (config.pin_flags & PIO_UART_DRIVER_PIN_FLAG_TX_FLOW_CONTROL) != 0u;
            TEST_ASSERT_EQUAL_PTR(expected_pio[index], config.pio);
            TEST_ASSERT_EQUAL_UINT32(expected_tx_state_machines[index], config.tx_state_machine);
            TEST_ASSERT_EQUAL_UINT32(expected_rx_state_machines[index], config.rx_state_machine);
            TEST_ASSERT_EQUAL_UINT32(expected_rts_pins[index], config.port->rts_pin);
            TEST_ASSERT_EQUAL_UINT32(expected_cts_pins[index], config.port->cts_pin);
            TEST_ASSERT_EQUAL_UINT32(PIO_UART_DRIVER_PIN_FLAG_RX_PULL_UP |
                                         PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH,
                                     config.pin_flags);
            TEST_ASSERT_EQUAL_UINT32(PICO_UART_PIO_UART_TX_DMA_START_THRESHOLD, config.tx_dma_start_threshold);
        }
    }

    TEST_ASSERT_TRUE(uart_board_topology_validate(topology, UART_PORT_COUNT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_actual_board_table_resolves_to_unique_backend_resources);
    return UNITY_END();
}