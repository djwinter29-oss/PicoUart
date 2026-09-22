/**
 * @file test_worker.c
 * @brief Host-native Unity tests for UART worker scheduling.
 */

#include "unity.h"

#include "uart/worker.h"

static uint8_t worker_order[2];
static size_t worker_order_length;

static void worker_service_control(void)
{
    worker_order[worker_order_length++] = 1u;
}

static void worker_poll_io(void)
{
    worker_order[worker_order_length++] = 2u;
}

void setUp(void)
{
    worker_order_length = 0u;
}

void tearDown(void)
{
}

void test_worker_step_services_control_before_io_and_publishes_heartbeat(void)
{
    volatile uint32_t heartbeat = 4u;
    const uart_worker_hooks_t hooks = {
        .service_control = worker_service_control,
        .poll_io = worker_poll_io,
        .heartbeat = &heartbeat,
    };

    uart_worker_step(&hooks);

    TEST_ASSERT_EQUAL_UINT(2u, worker_order_length);
    TEST_ASSERT_EQUAL_UINT8(1u, worker_order[0]);
    TEST_ASSERT_EQUAL_UINT8(2u, worker_order[1]);
    TEST_ASSERT_EQUAL_UINT32(5u, heartbeat);
}

void test_worker_step_rejects_incomplete_hooks(void)
{
    volatile uint32_t heartbeat = 4u;
    const uart_worker_hooks_t hooks = {.heartbeat = &heartbeat};

    uart_worker_step(NULL);
    uart_worker_step(&hooks);

    TEST_ASSERT_EQUAL_UINT(0u, worker_order_length);
    TEST_ASSERT_EQUAL_UINT32(4u, heartbeat);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_worker_step_services_control_before_io_and_publishes_heartbeat);
    RUN_TEST(test_worker_step_rejects_incomplete_hooks);
    return UNITY_END();
}