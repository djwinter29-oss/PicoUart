// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/** @file flash.h
 * @brief Host declarations for Pico SDK flash programming APIs.
 */
#ifndef HARDWARE_FLASH_H
#define HARDWARE_FLASH_H

#include <stddef.h>
#include <stdint.h>

#define FLASH_PAGE_SIZE 256u

void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count);

#endif