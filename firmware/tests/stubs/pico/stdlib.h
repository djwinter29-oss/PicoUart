/**
 * @file stdlib.h
 * @brief Host-test stub for the Pico SDK worker-loop helper.
 */

#ifndef PICO_STDLIB_H
#define PICO_STDLIB_H

#include "pico/time.h"
#include <assert.h>

/** @brief Mirror the SDK invariant assertion in native tests. */
#define hard_assert(condition) assert(condition)

static inline void tight_loop_contents(void)
{
}

#endif