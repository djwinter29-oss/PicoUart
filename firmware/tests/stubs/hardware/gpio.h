// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
/**
 * @file gpio.h
 * @brief Host declarations for identification LED GPIO calls.
 */
#ifndef HARDWARE_GPIO_H
#define HARDWARE_GPIO_H

#include <stdbool.h>

/** @brief GPIO output direction. */
#define GPIO_OUT true

void gpio_init(unsigned int gpio);
void gpio_set_dir(unsigned int gpio, bool output);
void gpio_put(unsigned int gpio, bool value);

#endif