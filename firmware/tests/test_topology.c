/**
 * @file test_topology.c
 * @brief Unity tests for normalized UART board-topology validation.
 */

#include "unity.h"

#include "uart/topology.h"

static void test_topology_make_valid(uart_topology_port_t ports[UART_PORT_COUNT])
{
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        bool is_hardware = index < 2u;
        uint32_t state_machine = (uint32_t)((index % 2u) * 2u);

        ports[index] = (uart_topology_port_t){
            .id = (uart_port_id_t)index,
            .backend = is_hardware ? UART_DRIVER_BACKEND_HW : UART_DRIVER_BACKEND_PIO,
            .baud_rate = 115200u,
            .tx_pin = (uint32_t)(index * 4u),
            .rx_pin = (uint32_t)(index * 4u + 1u),
            .backend_instance = is_hardware ? (uintptr_t)(index + 1u) :
                                               (uintptr_t)(index < 4u ? 3u : 4u),
            .backend_baud_rate = 115200u,
            .backend_tx_pin = (uint32_t)(index * 4u),
            .backend_rx_pin = (uint32_t)(index * 4u + 1u),
            .tx_state_machine = state_machine,
            .rx_state_machine = state_machine + 1u,
            .target_gpio_count = 30u,
            .rts_pin = UART_TOPOLOGY_PIN_UNASSIGNED,
            .cts_pin = UART_TOPOLOGY_PIN_UNASSIGNED,
        };
    }
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_valid_six_port_topology(void)
{
    uart_topology_port_t ports[UART_PORT_COUNT];

    test_topology_make_valid(ports);
    TEST_ASSERT_TRUE(uart_topology_validate(ports, UART_PORT_COUNT));
}

void test_rejects_wrong_port_count_or_identity(void)
{
    uart_topology_port_t ports[UART_PORT_COUNT];

    test_topology_make_valid(ports);
    TEST_ASSERT_FALSE(uart_topology_validate(NULL, UART_PORT_COUNT));
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT - 1u));
    ports[3].id = UART_PORT_2;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));
}

void test_rejects_invalid_pin_assignments(void)
{
    uart_topology_port_t ports[UART_PORT_COUNT];

    test_topology_make_valid(ports);
    ports[0].tx_pin = UART_TOPOLOGY_PIN_UNASSIGNED;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));

    test_topology_make_valid(ports);
    ports[0].rx_pin = ports[0].tx_pin;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));

    test_topology_make_valid(ports);
    ports[3].tx_pin = ports[2].rx_pin;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));

    test_topology_make_valid(ports);
    ports[5].rx_pin = 30u;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));
}

void test_optional_flow_control_pins_collide_only_when_enabled(void)
{
    uart_topology_port_t ports[UART_PORT_COUNT];

    test_topology_make_valid(ports);
    ports[0].rts_pin = ports[1].tx_pin;
    TEST_ASSERT_TRUE(uart_topology_validate(ports, UART_PORT_COUNT));

    ports[0].rts_enabled = true;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));

    test_topology_make_valid(ports);
    ports[2].cts_pin = 29u;
    ports[2].cts_enabled = true;
    TEST_ASSERT_TRUE(uart_topology_validate(ports, UART_PORT_COUNT));
    ports[3].rts_pin = 29u;
    ports[3].rts_enabled = true;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));

    test_topology_make_valid(ports);
    ports[4].cts_pin = 30u;
    ports[4].cts_enabled = true;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));
}

void test_rejects_backend_metadata_mismatches(void)
{
    uart_topology_port_t ports[UART_PORT_COUNT];

    test_topology_make_valid(ports);
    ports[0].backend_instance = 0u;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));

    test_topology_make_valid(ports);
    ports[0].backend_baud_rate = 9600u;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));

    test_topology_make_valid(ports);
    ports[0].backend_tx_pin = ports[0].rx_pin;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));
}

void test_rejects_duplicate_hardware_or_pio_resources(void)
{
    uart_topology_port_t ports[UART_PORT_COUNT];

    test_topology_make_valid(ports);
    ports[1].backend_instance = ports[0].backend_instance;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));

    test_topology_make_valid(ports);
    ports[3].tx_state_machine = ports[2].rx_state_machine;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));

    test_topology_make_valid(ports);
    ports[2].rx_state_machine = 4u;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));
}

void test_rejects_invalid_backend_balance(void)
{
    uart_topology_port_t ports[UART_PORT_COUNT];

    test_topology_make_valid(ports);
    ports[2].backend = UART_DRIVER_BACKEND_HW;
    TEST_ASSERT_FALSE(uart_topology_validate(ports, UART_PORT_COUNT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_six_port_topology);
    RUN_TEST(test_rejects_wrong_port_count_or_identity);
    RUN_TEST(test_rejects_invalid_pin_assignments);
    RUN_TEST(test_optional_flow_control_pins_collide_only_when_enabled);
    RUN_TEST(test_rejects_backend_metadata_mismatches);
    RUN_TEST(test_rejects_duplicate_hardware_or_pio_resources);
    RUN_TEST(test_rejects_invalid_backend_balance);
    return UNITY_END();
}
