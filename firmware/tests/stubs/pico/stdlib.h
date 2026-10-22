/**
 * @file stdlib.h
 * @brief Host-test stub for the Pico SDK worker-loop helper.
 */

#ifndef PICO_STDLIB_H
#define PICO_STDLIB_H

#include "pico/time.h"
#include <assert.h>

#ifndef PICO_OK
#define PICO_OK 0
#endif
#ifndef __not_in_flash_func
#define __not_in_flash_func(function) function
#endif

/** @brief Mirror the SDK invariant assertion in native tests. */
#define hard_assert(condition) assert(condition)

static inline void tight_loop_contents(void)
{
}

#endif