/**
 * @file sync.h
 * @brief Host-test stub for Pico SDK hardware/sync.h.
 *
 * Only the barrier used by ring_buffer is required on the host toolchain.
 */

#ifndef HARDWARE_SYNC_H
#define HARDWARE_SYNC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct spin_lock spin_lock_t;

static inline void __dmb(void)
{
}

static inline uint32_t spin_lock_blocking(spin_lock_t *lock)
{
	(void)lock;
	return 0u;
}

static inline void spin_unlock(spin_lock_t *lock, uint32_t save)
{
	(void)lock;
	(void)save;
}

static inline unsigned int spin_lock_claim_unused(bool required)
{
	(void)required;
	return 0u;
}

static inline spin_lock_t *spin_lock_instance(unsigned int lock)
{
	(void)lock;
	return (spin_lock_t *)1;
}

#endif
