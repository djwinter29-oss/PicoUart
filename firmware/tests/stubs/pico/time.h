/**
 * @file time.h
 * @brief Host-test stub for Pico SDK pico/time.h.
 */

#ifndef PICO_TIME_H
#define PICO_TIME_H

#include <stdbool.h>
#include <stdint.h>

typedef int64_t absolute_time_t;

extern absolute_time_t pico_test_time_us;

#define nil_time ((absolute_time_t)0)

static inline absolute_time_t make_timeout_time_ms(uint32_t timeout_ms)
{
    return pico_test_time_us + ((absolute_time_t)timeout_ms * 1000);
}

static inline bool time_reached(absolute_time_t deadline)
{
    return pico_test_time_us >= deadline;
}

static inline absolute_time_t get_absolute_time(void)
{
    return pico_test_time_us;
}

static inline uint32_t to_ms_since_boot(absolute_time_t time)
{
    return (uint32_t)(time / 1000);
}

#endif