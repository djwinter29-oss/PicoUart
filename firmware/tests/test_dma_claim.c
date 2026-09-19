/**
 * @file test_dma_claim.c
 * @brief Fault-injection tests for HW UART DMA claim/rollback (see dma_claim.h).
 *
 * These tests link the real production hw_uart_driver_claim_dma_channels()
 * from dma_claim.c and drive it through a fake ops table, exercising the
 * actual cleanup path rather than re-modeling it.
 */

#include "unity.h"
#include "uart/hw/dma_claim.h"

#include <stddef.h>

/** @brief Call log entry kind recorded by the fake ops. */
typedef enum {
    FAKE_CALL_CLAIM,
    FAKE_CALL_UNCLAIM,
} fake_call_kind_t;

/** @brief One recorded call into the fake claim/unclaim ops. */
typedef struct {
    fake_call_kind_t kind;
    int channel; /**< Channel argument for FAKE_CALL_UNCLAIM. */
} fake_call_t;

#define FAKE_CALL_LOG_CAPACITY 8u

static fake_call_t fake_call_log[FAKE_CALL_LOG_CAPACITY];
static size_t fake_call_count;
static int fake_claim_results[FAKE_CALL_LOG_CAPACITY];
static size_t fake_claim_result_index;

static int fake_claim_channel(bool required)
{
    (void)required;
    TEST_ASSERT_LESS_THAN_size_t(FAKE_CALL_LOG_CAPACITY, fake_call_count);
    fake_call_log[fake_call_count].kind = FAKE_CALL_CLAIM;
    fake_call_log[fake_call_count].channel = -1;
    fake_call_count++;

    TEST_ASSERT_LESS_THAN_size_t(FAKE_CALL_LOG_CAPACITY, fake_claim_result_index);
    return fake_claim_results[fake_claim_result_index++];
}

static void fake_unclaim_channel(unsigned int channel)
{
    TEST_ASSERT_LESS_THAN_size_t(FAKE_CALL_LOG_CAPACITY, fake_call_count);
    fake_call_log[fake_call_count].kind = FAKE_CALL_UNCLAIM;
    fake_call_log[fake_call_count].channel = (int)channel;
    fake_call_count++;
}

static const hw_uart_dma_claim_ops_t fake_ops = {
    .claim_channel = fake_claim_channel,
    .unclaim_channel = fake_unclaim_channel,
};

void setUp(void)
{
    fake_call_count = 0u;
    fake_claim_result_index = 0u;
    for (size_t i = 0u; i < FAKE_CALL_LOG_CAPACITY; ++i) {
        fake_claim_results[i] = -1;
    }
}

void tearDown(void)
{
}

/** @brief RX claim succeeds and TX claim also succeeds: both channels kept. */
void test_both_channels_claimed_on_success(void)
{
    int rx = -1;
    int tx = -1;

    fake_claim_results[0] = 3;
    fake_claim_results[1] = 5;

    TEST_ASSERT_TRUE(hw_uart_driver_claim_dma_channels(&fake_ops, &rx, &tx));
    TEST_ASSERT_EQUAL_INT(3, rx);
    TEST_ASSERT_EQUAL_INT(5, tx);
    TEST_ASSERT_EQUAL_size_t(2u, fake_call_count);
}

/**
 * @brief RX DMA claim succeeds but TX DMA claim fails: the production
 *        rollback in hw_uart_driver_claim_dma_channels() must unclaim RX and
 *        reset both outputs to -1.
 */
void test_rx_claim_released_when_tx_claim_fails(void)
{
    int rx = 7;
    int tx = 7;

    fake_claim_results[0] = 2; /* RX claim succeeds with channel 2. */
    fake_claim_results[1] = -1; /* TX claim exhausted. */

    TEST_ASSERT_FALSE(hw_uart_driver_claim_dma_channels(&fake_ops, &rx, &tx));

    TEST_ASSERT_EQUAL_INT(-1, rx);
    TEST_ASSERT_EQUAL_INT(-1, tx);

    /* Exactly claim(rx), claim(tx), unclaim(rx) — RX must be released. */
    TEST_ASSERT_EQUAL_size_t(3u, fake_call_count);
    TEST_ASSERT_EQUAL(FAKE_CALL_CLAIM, fake_call_log[0].kind);
    TEST_ASSERT_EQUAL(FAKE_CALL_CLAIM, fake_call_log[1].kind);
    TEST_ASSERT_EQUAL(FAKE_CALL_UNCLAIM, fake_call_log[2].kind);
    TEST_ASSERT_EQUAL_INT(2, fake_call_log[2].channel);
}

/** @brief RX DMA claim itself fails: no unclaim should be attempted,
 *          and `tx_dma_channel` is not written by the production code
 *          (its output is undefined on failure). */
void test_no_unclaim_when_rx_claim_fails(void)
{
    int rx = 7;
    int tx_dummy = 7;

    fake_claim_results[0] = -1;

    TEST_ASSERT_FALSE(hw_uart_driver_claim_dma_channels(&fake_ops, &rx, &tx_dummy));
    TEST_ASSERT_EQUAL_INT(-1, rx);
    /* tx_dma_channel is not written by production code on early RX failure. */
    TEST_ASSERT_EQUAL_size_t(1u, fake_call_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_both_channels_claimed_on_success);
    RUN_TEST(test_rx_claim_released_when_tx_claim_fails);
    RUN_TEST(test_no_unclaim_when_rx_claim_fails);
    return UNITY_END();
}
