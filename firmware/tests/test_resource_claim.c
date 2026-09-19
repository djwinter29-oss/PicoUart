/**
 * @file test_resource_claim.c
 * @brief Fault-injection tests for PIO resource claim/rollback.
 *
 * These tests link the production resource_claim.c algorithm and use only the
 * injectable ops table; they do not duplicate the cleanup implementation.
 */

#include "unity.h"
#include "uart/pio/resource_claim.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    FAKE_SM_IS_CLAIMED,
    FAKE_SM_CLAIM,
    FAKE_SM_UNCLAIM,
    FAKE_DMA_CLAIM,
    FAKE_DMA_UNCLAIM,
} fake_call_kind_t;

typedef struct {
    fake_call_kind_t kind;
    unsigned int resource;
} fake_call_t;

#define FAKE_CALL_LOG_CAPACITY 16u

static fake_call_t calls[FAKE_CALL_LOG_CAPACITY];
static size_t call_count;
static int dma_results[4];
static size_t dma_result_index;
static unsigned int claimed_sm_mask;

static void record_call(fake_call_kind_t kind, unsigned int resource)
{
    TEST_ASSERT_LESS_THAN_size_t(FAKE_CALL_LOG_CAPACITY, call_count);
    calls[call_count++] = (fake_call_t){kind, resource};
}

static bool fake_sm_is_claimed(void *pio, unsigned int sm)
{
    (void)pio;
    record_call(FAKE_SM_IS_CLAIMED, sm);
    return (claimed_sm_mask & (1u << sm)) != 0u;
}

static void fake_sm_claim(void *pio, unsigned int sm)
{
    (void)pio;
    record_call(FAKE_SM_CLAIM, sm);
    claimed_sm_mask |= 1u << sm;
}

static void fake_sm_unclaim(void *pio, unsigned int sm)
{
    (void)pio;
    record_call(FAKE_SM_UNCLAIM, sm);
    claimed_sm_mask &= ~(1u << sm);
}

static int fake_dma_claim(bool required)
{
    (void)required;
    record_call(FAKE_DMA_CLAIM, (unsigned int)dma_result_index);
    return dma_results[dma_result_index++];
}

static void fake_dma_unclaim(unsigned int channel)
{
    record_call(FAKE_DMA_UNCLAIM, channel);
}

static const pio_uart_resource_claim_ops_t fake_ops = {
    .sm_is_claimed = fake_sm_is_claimed,
    .sm_claim = fake_sm_claim,
    .sm_unclaim = fake_sm_unclaim,
    .claim_dma_channel = fake_dma_claim,
    .unclaim_dma_channel = fake_dma_unclaim,
};

void setUp(void)
{
    call_count = 0u;
    dma_result_index = 0u;
    claimed_sm_mask = 0u;
    for (size_t i = 0u; i < 4u; ++i) {
        dma_results[i] = -1;
    }
}

void tearDown(void) {}

static void assert_failed_outputs(bool tx_claimed,
                                  bool rx_claimed,
                                  int rx_dma,
                                  int tx_dma)
{
    TEST_ASSERT_FALSE(tx_claimed);
    TEST_ASSERT_FALSE(rx_claimed);
    TEST_ASSERT_EQUAL_INT(-1, rx_dma);
    TEST_ASSERT_EQUAL_INT(-1, tx_dma);
    TEST_ASSERT_EQUAL_UINT(0u, claimed_sm_mask);
}

void test_all_resources_remain_claimed_on_success(void)
{
    bool tx_sm_claimed = true;
    bool rx_sm_claimed = true;
    int rx_dma = 9;
    int tx_dma = 9;
    dma_results[0] = 2;
    dma_results[1] = 3;

    TEST_ASSERT_TRUE(pio_uart_driver_claim_resources(&fake_ops, (void *)1u, 1u, 2u,
                                                     &tx_sm_claimed, &rx_sm_claimed,
                                                     &rx_dma, &tx_dma));
    TEST_ASSERT_TRUE(tx_sm_claimed);
    TEST_ASSERT_TRUE(rx_sm_claimed);
    TEST_ASSERT_EQUAL_INT(2, rx_dma);
    TEST_ASSERT_EQUAL_INT(3, tx_dma);
    TEST_ASSERT_EQUAL_UINT((1u << 1) | (1u << 2), claimed_sm_mask);
}

void test_rx_dma_failure_releases_both_state_machines(void)
{
    bool tx_sm_claimed = true;
    bool rx_sm_claimed = true;
    int rx_dma = 9;
    int tx_dma = 9;

    TEST_ASSERT_FALSE(pio_uart_driver_claim_resources(&fake_ops, (void *)1u, 1u, 2u,
                                                      &tx_sm_claimed, &rx_sm_claimed,
                                                      &rx_dma, &tx_dma));
    assert_failed_outputs(tx_sm_claimed, rx_sm_claimed, rx_dma, tx_dma);
    /* 2 is_claimed + 2 sm_claim + 1 dma_claim (rx, fails) + 2 sm_unclaim. */
    TEST_ASSERT_EQUAL_UINT(7u, call_count);
    TEST_ASSERT_EQUAL(FAKE_SM_UNCLAIM, calls[5].kind);
    TEST_ASSERT_EQUAL_UINT(2u, calls[5].resource);
    TEST_ASSERT_EQUAL(FAKE_SM_UNCLAIM, calls[6].kind);
    TEST_ASSERT_EQUAL_UINT(1u, calls[6].resource);
}

void test_tx_dma_failure_releases_rx_dma_and_both_state_machines(void)
{
    bool tx_sm_claimed = true;
    bool rx_sm_claimed = true;
    int rx_dma = 9;
    int tx_dma = 9;
    dma_results[0] = 4;
    dma_results[1] = -1;

    TEST_ASSERT_FALSE(pio_uart_driver_claim_resources(&fake_ops, (void *)1u, 1u, 2u,
                                                      &tx_sm_claimed, &rx_sm_claimed,
                                                      &rx_dma, &tx_dma));
    assert_failed_outputs(tx_sm_claimed, rx_sm_claimed, rx_dma, tx_dma);
    /* 2 is_claimed + 2 sm_claim + 2 dma_claim (rx ok, tx fails) + 1 dma_unclaim
     * (rx) + 2 sm_unclaim. */
    TEST_ASSERT_EQUAL_UINT(9u, call_count);
    TEST_ASSERT_EQUAL(FAKE_DMA_UNCLAIM, calls[6].kind);
    TEST_ASSERT_EQUAL_UINT(4u, calls[6].resource);
    TEST_ASSERT_EQUAL(FAKE_SM_UNCLAIM, calls[7].kind);
    TEST_ASSERT_EQUAL_UINT(2u, calls[7].resource);
    TEST_ASSERT_EQUAL(FAKE_SM_UNCLAIM, calls[8].kind);
    TEST_ASSERT_EQUAL_UINT(1u, calls[8].resource);
}

void test_preclaimed_state_machine_rejects_without_cleanup(void)
{
    bool tx_sm_claimed = true;
    bool rx_sm_claimed = true;
    int rx_dma = 9;
    int tx_dma = 9;
    claimed_sm_mask = 1u << 1;

    TEST_ASSERT_FALSE(pio_uart_driver_claim_resources(&fake_ops, (void *)1u, 1u, 2u,
                                                      &tx_sm_claimed, &rx_sm_claimed,
                                                      &rx_dma, &tx_dma));
    TEST_ASSERT_FALSE(tx_sm_claimed);
    TEST_ASSERT_FALSE(rx_sm_claimed);
    TEST_ASSERT_EQUAL_INT(-1, rx_dma);
    TEST_ASSERT_EQUAL_INT(-1, tx_dma);
    /* Short-circuits on the first is_claimed() check (tx_sm already
     * claimed); rx_sm is never even queried, and nothing is unclaimed. */
    TEST_ASSERT_EQUAL_UINT(1u, call_count);
    TEST_ASSERT_EQUAL_UINT(1u << 1, claimed_sm_mask);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_resources_remain_claimed_on_success);
    RUN_TEST(test_rx_dma_failure_releases_both_state_machines);
    RUN_TEST(test_tx_dma_failure_releases_rx_dma_and_both_state_machines);
    RUN_TEST(test_preclaimed_state_machine_rejects_without_cleanup);
    return UNITY_END();
}