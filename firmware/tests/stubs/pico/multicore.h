// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file multicore.h
 * @brief Host-test stub for Pico SDK pico/multicore.h.
 */

#ifndef PICO_MULTICORE_H
#define PICO_MULTICORE_H

typedef void (*pico_test_core_entry_t)(void);
extern void (*test_multicore_launch_hook)(pico_test_core_entry_t entry);
extern void (*test_multicore_reset_hook)(void);

static inline void multicore_reset_core1(void)
{
    if (test_multicore_reset_hook != 0) {
        test_multicore_reset_hook();
    }
}

static inline void multicore_launch_core1(pico_test_core_entry_t entry)
{
    if (test_multicore_launch_hook != 0) {
        test_multicore_launch_hook(entry);
    }
}

#endif