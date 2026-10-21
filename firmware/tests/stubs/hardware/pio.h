/**
 * @file pio.h
 * @brief Host-test stub for Pico SDK hardware/pio.h.
 */

#ifndef HARDWARE_PIO_H
#define HARDWARE_PIO_H

typedef struct pio_hw {
	unsigned int test_identity;
} pio_hw_t;

typedef pio_hw_t *PIO;

extern pio_hw_t pio0_test_instance;
extern pio_hw_t pio1_test_instance;

#define pio0 (&pio0_test_instance)
#define pio1 (&pio1_test_instance)

#endif