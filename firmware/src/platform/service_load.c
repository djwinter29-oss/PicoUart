/**
 * @file service_load.c
 * @brief Single-writer per-core service windows with atomic result publication.
 */
#include "platform/service_load.h"

#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>

/** @brief One second in the SDK microsecond timebase. */
#define SERVICE_LOAD_WINDOW_US 1000000u

/** @brief A core owns all window state; only the published ratio crosses cores. */
typedef struct {
    uint32_t window_start; /**< Timer at the first pass after the previous publication. */
    uint32_t pass_start;   /**< Timer at the start of the current service pass. */
    uint64_t busy_us;      /**< Wall time of marked passes in the current window. */
    bool started;         /**< True once the first pass establishes a time origin. */
    bool measuring;       /**< True between begin and end; ignores initialization work. */
    bool worked;          /**< True when this pass handles at least one work item. */
    _Atomic uint32_t published; /**< Last completed ratio, atomically read by HID on core 0. */
} service_load_core_t;

/** @brief Independent, single-writer windows for the two service cores. */
static service_load_core_t cores[2];

#if PICO_UART_PROFILE_TIMING
volatile uint32_t service_load_max_pass_us[2];
volatile uint32_t service_load_max_gap_us[2];
#endif

void service_load_init(void)
{
    for (unsigned int core = 0; core < 2u; ++core) {
    #if PICO_UART_PROFILE_TIMING
        service_load_max_pass_us[core] = 0u;
        service_load_max_gap_us[core] = 0u;
    #endif
        cores[core].started = false;
        cores[core].measuring = false;
        cores[core].worked = false;
        cores[core].busy_us = 0u;
        atomic_init(&cores[core].published, SERVICE_LOAD_UNAVAILABLE);
    }
}

void service_load_begin(unsigned int core, uint32_t now_us)
{
    assert(core < 2u);
    service_load_core_t *state = &cores[core];
#if PICO_UART_PROFILE_TIMING
    if (state->started) {
        uint32_t gap = now_us - state->pass_start;
        if (gap > service_load_max_gap_us[core]) service_load_max_gap_us[core] = gap;
    }
#endif
    if (!state->started) {
        state->window_start = now_us;
        state->started = true;
    }
    state->pass_start = now_us;
    state->worked = false;
    state->measuring = true;
}

void service_load_note(unsigned int core)
{
    assert(core < 2u);
    if (cores[core].measuring) {
        cores[core].worked = true;
    }
}

void service_load_end(unsigned int core, uint32_t now_us)
{
    assert(core < 2u);
    service_load_core_t *state = &cores[core];
#if PICO_UART_PROFILE_TIMING
    if (state->measuring) {
        uint32_t duration = now_us - state->pass_start;
        if (duration > service_load_max_pass_us[core]) service_load_max_pass_us[core] = duration;
    }
#endif
    if (state->worked) {
        state->busy_us += (uint32_t)(now_us - state->pass_start);
    }
    state->measuring = false;
    uint32_t elapsed = now_us - state->window_start;
    if (elapsed >= SERVICE_LOAD_WINDOW_US) {
        uint32_t ratio = (uint32_t)(state->busy_us * 10000u / elapsed);
        atomic_store_explicit(&state->published, ratio, memory_order_relaxed);
        state->window_start = now_us;
        state->busy_us = 0u;
    }
}

uint16_t service_load_read(unsigned int core)
{
    assert(core < 2u);
    return (uint16_t)atomic_load_explicit(&cores[core].published, memory_order_relaxed);
}
