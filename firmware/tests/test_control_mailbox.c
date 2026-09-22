/**
 * @file test_control_mailbox.c
 * @brief Host-native Unity tests for UART control mailbox handoff.
 */

#include "unity.h"

#include "uart/control/mailbox.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_publish_and_take_preserve_request_payload(void)
{
    uart_control_mailbox_t mailbox;
    uart_control_mailbox_request_t request = {
        .port_id = 3u,
        .control_generation = 7u,
        .tx_boundary_sequence = 11u,
        .line_coding = {
            .baud_rate = 230400u,
            .data_bits = 8u,
            .stop_bits = 1u,
            .parity = UART_DRIVER_PARITY_NONE,
        },
    };
    uart_control_mailbox_request_t received;

    uart_control_mailbox_reset(&mailbox);
    TEST_ASSERT_TRUE(uart_control_mailbox_publish(&mailbox, &request));
    TEST_ASSERT_TRUE(uart_control_mailbox_has_pending_port(&mailbox, request.port_id));
    TEST_ASSERT_FALSE(uart_control_mailbox_publish(&mailbox, &request));
    TEST_ASSERT_TRUE(uart_control_mailbox_take(&mailbox, &received));
    TEST_ASSERT_EQUAL_UINT32(request.port_id, received.port_id);
    TEST_ASSERT_EQUAL_UINT32(request.control_generation, received.control_generation);
    TEST_ASSERT_EQUAL_UINT32(request.tx_boundary_sequence, received.tx_boundary_sequence);
    TEST_ASSERT_EQUAL_UINT32(request.line_coding.baud_rate, received.line_coding.baud_rate);
    TEST_ASSERT_FALSE(uart_control_mailbox_has_pending_port(&mailbox, request.port_id));
    TEST_ASSERT_FALSE(uart_control_mailbox_take(&mailbox, &received));
}

void test_publish_wraps_request_sequence(void)
{
    uart_control_mailbox_t mailbox;
    uart_control_mailbox_request_t request = {0};

    uart_control_mailbox_reset(&mailbox);
    mailbox.request_sequence = UINT32_MAX;
    mailbox.response_sequence = UINT32_MAX;

    TEST_ASSERT_TRUE(uart_control_mailbox_publish(&mailbox, &request));
    TEST_ASSERT_EQUAL_UINT32(0u, mailbox.request_sequence);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_publish_and_take_preserve_request_payload);
    RUN_TEST(test_publish_wraps_request_sequence);
    return UNITY_END();
}