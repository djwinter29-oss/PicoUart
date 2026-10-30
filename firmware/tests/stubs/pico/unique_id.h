// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/** @file unique_id.h
 * @brief Host-test declarations for the SDK board serial API.
 */
#ifndef PICO_UNIQUE_ID_H
#define PICO_UNIQUE_ID_H

#include <stddef.h>
#include <stdint.h>

/** @brief SDK unique board ID size on RP2040 and RP2350. */
#define PICO_UNIQUE_BOARD_ID_SIZE_BYTES 8u

typedef struct {
    uint8_t id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
} pico_unique_board_id_t;

void pico_get_unique_board_id(pico_unique_board_id_t *id);

/** @brief Write the test fixture serial as NUL-terminated ASCII hex. */
void pico_get_unique_board_id_string(char *id_out, size_t len);

#endif
