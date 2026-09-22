/**
 * @file test_control_plane.c
 * @brief Host-native Unity tests for deferred UART line-coding control.
 */

#include "unity.h"

#include "uart/control/plane.h"

absolute_time_t pico_test_time_us;

static ring_buffer_t test_tx_ring;
static uart_driver_line_coding_t applied_line_coding;
static uint32_t apply_count;
static bool apply_result;
static bool line_coding_matches;
static bool backend_alive;
static bool fail_closed;

static ring_buffer_t *test_tx_ring_for_backend(uart_backend_instance_t *instance)
{
    (void)instance;
    return &test_tx_ring;
}

static bool test_line_coding_matches(const uart_backend_instance_t *instance,
                                     const uart_driver_line_coding_t *line_coding)
{
    (void)instance;
    (void)line_coding;
    return line_coding_matches;
}

static bool test_line_coding_acceptable(const uart_driver_line_coding_t *line_coding)
{
    return line_coding->data_bits == 8u;
}

static bool test_is_initialized(const uart_backend_instance_t *instance)
{
    (void)instance;
    return backend_alive;
}

static bool test_set_line_coding(uart_backend_instance_t *instance,
                                 const uart_driver_line_coding_t *line_coding)
{
    (void)instance;
    if (fail_closed) {
        backend_alive = false;
        return false;
    }

    applied_line_coding = *line_coding;
    apply_count += 1u;
    return apply_result;
}

static uint32_t test_baud_rate(const uart_backend_instance_t *instance)
{
    (void)instance;
    return applied_line_coding.baud_rate;
}

static const uart_backend_ops_t test_backend_ops = {
    .is_initialized = test_is_initialized,
    .tx_ring = test_tx_ring_for_backend,
    .line_coding_matches = test_line_coding_matches,
    .line_coding_acceptable = test_line_coding_acceptable,
    .set_line_coding = test_set_line_coding,
    .baud_rate = test_baud_rate,
};

static uart_runtime_port_t test_ports[UART_PORT_COUNT];
static uart_control_mailbox_t test_mailboxes[UART_PORT_COUNT];
static uart_control_pending_t test_pending_controls[UART_PORT_COUNT];
static bool test_soft_pending_controls[UART_PORT_COUNT];
static uint32_t test_control_generations[UART_PORT_COUNT];
static volatile uint8_t test_status_flags[UART_PORT_COUNT];
static volatile uint32_t test_stats_sequence[UART_PORT_COUNT];
static size_t test_poll_start_index;
static uart_control_plane_t test_control_plane;

static uart_driver_line_coding_t test_line_coding(void)
{
    return (uart_driver_line_coding_t){
        .baud_rate = 230400u,
        .data_bits = 8u,
        .stop_bits = 1u,
        .parity = UART_DRIVER_PARITY_NONE,
    };
}

static void publish_request_for_port(uint32_t port_id, uint32_t tx_boundary_sequence)
{
    uart_control_mailbox_request_t request = {
        .port_id = port_id,
        .control_generation = 1u,
        .tx_boundary_sequence = tx_boundary_sequence,
        .line_coding = test_line_coding(),
    };

    uart_control_mailbox_t *slot =
        &test_mailboxes[(port_id < UART_PORT_COUNT) ? port_id : UART_PORT_0];

    TEST_ASSERT_TRUE(uart_control_mailbox_publish(slot, &request));
}

static void publish_request(uint32_t tx_boundary_sequence)
{
    publish_request_for_port(UART_PORT_0, tx_boundary_sequence);
}

void setUp(void)
{
    test_tx_ring = (ring_buffer_t){0};
    applied_line_coding = (uart_driver_line_coding_t){0};
    apply_count = 0u;
    apply_result = true;
    line_coding_matches = false;
    backend_alive = true;
    fail_closed = false;
    pico_test_time_us = 0;
    for (size_t index = 0u; index < UART_PORT_COUNT; ++index) {
        test_ports[index] = (index == UART_PORT_0)
                                ? (uart_runtime_port_t){.ops = &test_backend_ops}
                                : (uart_runtime_port_t){0};
        uart_control_mailbox_reset(&test_mailboxes[index]);
        test_pending_controls[index] = (uart_control_pending_t){0};
        test_soft_pending_controls[index] = false;
        test_control_generations[index] = (index == UART_PORT_0) ? 1u : 0u;
        test_status_flags[index] =
            (index == UART_PORT_0) ? UART_DRIVER_PORT_STATUS_CONTROL_PENDING : 0u;
        test_stats_sequence[index] = 0u;
    }
    test_poll_start_index = 0u;
    test_control_plane = (uart_control_plane_t){
        .ports = test_ports,
        .mailboxes = test_mailboxes,
        .pending_controls = test_pending_controls,
        .soft_pending_controls = test_soft_pending_controls,
        .control_generations = test_control_generations,
        .status_flags = test_status_flags,
        .status_lock = (spin_lock_t *)1,
        .stats_sequence = test_stats_sequence,
        .poll_start_index = &test_poll_start_index,
    };
}

void tearDown(void)
{
}

void test_control_plane_waits_for_tx_boundary_before_applying(void)
{
    test_tx_ring.consumer = 4u;
    publish_request(5u);

    uart_control_plane_service(&test_control_plane);

    TEST_ASSERT_TRUE(test_pending_controls[UART_PORT_0].pending);
    TEST_ASSERT_EQUAL_UINT32(0u, apply_count);
    TEST_ASSERT_TRUE(uart_control_plane_tx_launch_allowed(&test_control_plane, UART_PORT_0));

    test_tx_ring.consumer = 5u;
    TEST_ASSERT_FALSE(uart_control_plane_tx_launch_allowed(&test_control_plane, UART_PORT_0));

    uart_control_plane_service(&test_control_plane);

    TEST_ASSERT_FALSE(test_pending_controls[UART_PORT_0].pending);
    TEST_ASSERT_EQUAL_UINT32(1u, apply_count);
    TEST_ASSERT_EQUAL_UINT32(230400u, test_ports[UART_PORT_0].info.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(0u, test_status_flags[UART_PORT_0]);
    TEST_ASSERT_TRUE(uart_control_plane_tx_launch_allowed(&test_control_plane, UART_PORT_0));
}

void test_control_plane_drops_invalid_mailbox_port(void)
{
    publish_request_for_port(UART_PORT_COUNT, 0u);

    uart_control_plane_service(&test_control_plane);

    TEST_ASSERT_TRUE(uart_control_mailbox_can_publish(&test_mailboxes[UART_PORT_0]));
    TEST_ASSERT_EQUAL_UINT32(0u, apply_count);
    TEST_ASSERT_BITS(UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                     UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                     test_status_flags[UART_PORT_0]);
    TEST_ASSERT_BITS(UART_DRIVER_PORT_STATUS_CONTROL_PENDING, 0u, test_status_flags[UART_PORT_0]);
}

void test_control_plane_rejects_payload_aimed_at_another_port(void)
{
    uart_control_mailbox_request_t request = {
        .port_id = UART_PORT_1,
        .control_generation = 1u,
        .tx_boundary_sequence = 0u,
        .line_coding = test_line_coding(),
    };

    test_ports[UART_PORT_1] = (uart_runtime_port_t){.ops = &test_backend_ops};
    TEST_ASSERT_TRUE(uart_control_mailbox_publish(&test_mailboxes[UART_PORT_0], &request));

    uart_control_plane_service(&test_control_plane);

    TEST_ASSERT_EQUAL_UINT32(0u, apply_count);
    TEST_ASSERT_FALSE(test_pending_controls[UART_PORT_1].pending);
    TEST_ASSERT_BITS(UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                     UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                     test_status_flags[UART_PORT_0]);
    TEST_ASSERT_BITS(UART_DRIVER_PORT_STATUS_CONTROL_PENDING, 0u, test_status_flags[UART_PORT_0]);
    TEST_ASSERT_EQUAL_UINT8(0u, test_status_flags[UART_PORT_1]);
}

void test_control_plane_stops_immediately_when_backend_is_retired(void)
{
    test_status_flags[UART_PORT_0] |= UART_DRIVER_PORT_STATUS_READY;
    fail_closed = true;
    publish_request(0u);

    uart_control_plane_service(&test_control_plane);

    TEST_ASSERT_FALSE(backend_alive);
    TEST_ASSERT_FALSE(test_pending_controls[UART_PORT_0].pending);
    TEST_ASSERT_EQUAL_UINT32(0u, apply_count);
    TEST_ASSERT_BITS(UART_DRIVER_PORT_STATUS_INIT_FAILED,
                     UART_DRIVER_PORT_STATUS_INIT_FAILED,
                     test_status_flags[UART_PORT_0]);
    TEST_ASSERT_BITS(UART_DRIVER_PORT_STATUS_READY, 0u, test_status_flags[UART_PORT_0]);
    TEST_ASSERT_BITS(UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                     UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                     test_status_flags[UART_PORT_0]);
    TEST_ASSERT_BITS(UART_DRIVER_PORT_STATUS_CONTROL_PENDING, 0u, test_status_flags[UART_PORT_0]);
}

void test_control_plane_reports_error_after_apply_timeout(void)
{
    test_tx_ring.consumer = 7u;
    apply_result = false;
    publish_request(7u);

    uart_control_plane_service(&test_control_plane);

    TEST_ASSERT_TRUE(test_pending_controls[UART_PORT_0].pending);
    TEST_ASSERT_EQUAL_UINT32(1u, apply_count);

    pico_test_time_us = 1000000;
    uart_control_plane_service(&test_control_plane);

    TEST_ASSERT_FALSE(test_pending_controls[UART_PORT_0].pending);
    TEST_ASSERT_BITS(UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                     UART_DRIVER_PORT_STATUS_CONTROL_ERROR,
                     test_status_flags[UART_PORT_0]);
    TEST_ASSERT_BITS(UART_DRIVER_PORT_STATUS_CONTROL_PENDING, 0u, test_status_flags[UART_PORT_0]);
}

void test_control_plane_applies_requests_on_independent_port_slots(void)
{
    test_ports[UART_PORT_1] = (uart_runtime_port_t){.ops = &test_backend_ops};
    test_control_generations[UART_PORT_1] = 1u;
    test_status_flags[UART_PORT_1] = UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    test_tx_ring.consumer = 0u;
    publish_request_for_port(UART_PORT_0, 0u);
    publish_request_for_port(UART_PORT_1, 0u);

    uart_control_plane_service(&test_control_plane);

    TEST_ASSERT_EQUAL_UINT32(2u, apply_count);
    TEST_ASSERT_EQUAL_size_t(0u, test_poll_start_index);
    TEST_ASSERT_FALSE(test_pending_controls[UART_PORT_0].pending);
    TEST_ASSERT_FALSE(test_pending_controls[UART_PORT_1].pending);
    TEST_ASSERT_TRUE(uart_control_mailbox_can_publish(&test_mailboxes[UART_PORT_0]));
    TEST_ASSERT_TRUE(uart_control_mailbox_can_publish(&test_mailboxes[UART_PORT_1]));
}

void test_control_plane_immediate_completion_clears_pending_when_unowned(void)
{
    line_coding_matches = true;
    test_status_flags[UART_PORT_0] = UART_DRIVER_PORT_STATUS_CONTROL_PENDING;
    publish_request(0u);

    uart_control_plane_service(&test_control_plane);

    TEST_ASSERT_FALSE(test_pending_controls[UART_PORT_0].pending);
    TEST_ASSERT_EQUAL_UINT32(0u, apply_count);
    TEST_ASSERT_EQUAL_UINT8(0u, test_status_flags[UART_PORT_0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_control_plane_waits_for_tx_boundary_before_applying);
    RUN_TEST(test_control_plane_drops_invalid_mailbox_port);
    RUN_TEST(test_control_plane_rejects_payload_aimed_at_another_port);
    RUN_TEST(test_control_plane_stops_immediately_when_backend_is_retired);
    RUN_TEST(test_control_plane_reports_error_after_apply_timeout);
    RUN_TEST(test_control_plane_applies_requests_on_independent_port_slots);
    RUN_TEST(test_control_plane_immediate_completion_clears_pending_when_unowned);
    return UNITY_END();
}