/**
 * @file pico_sdk_hooks.c
 * @brief Shared hook storage for native Pico SDK stubs.
 */

#include "hardware/sync.h"
#include "pico/multicore.h"

void (*test_spin_unlock_hook)(void) = NULL;
void (*test_multicore_launch_hook)(pico_test_core_entry_t entry) = NULL;
