// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/** @file flash.h
 * @brief Host declarations for the SDK multicore flash safety API.
 */
#ifndef PICO_FLASH_H
#define PICO_FLASH_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Initialize the calling core as a flash lockout victim. */
bool flash_safe_execute_core_init(void);
/** @brief Execute a callback only after both cores can safely pause XIP. */
int flash_safe_execute(void (*func)(void *), void *param, uint32_t timeout_ms);

#endif
