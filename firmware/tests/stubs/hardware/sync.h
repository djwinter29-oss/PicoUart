/**
 * @file sync.h
 * @brief Host-test stub for Pico SDK hardware/sync.h.
 *
 * Only the barrier used by ring_buffer is required on the host toolchain.
 */

#ifndef HARDWARE_SYNC_H
#define HARDWARE_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct spin_lock spin_lock_t;

static inline void __dmb(void)
{
}

/**
 * @brief Test-only hook fired at the exact moment a critical section ends.
 *
 * Host tests have no real second core, so a moved-out-of-the-lock bug cannot
 * be caught by racing threads. Instead, a test can point this at a probe that
 * asserts an invariant must already hold by the time the matching
 * spin_unlock() runs -- e.g. "if the mailbox looks acked, ownership must
 * already be registered". Left `NULL` outside of tests that opt in.
 *
 * This header is included by many production `.c` files linked into many
 * separate host-test executables, so the storage is a weak definition here
 * rather than an `extern` declared elsewhere: every executable gets its own
 * merged copy without a dedicated stub source file or per-target wiring.
 */
__attribute__((weak)) void (*test_spin_unlock_hook)(void) = NULL;

static inline uint32_t spin_lock_blocking(spin_lock_t *lock)
{
	(void)lock;
	return 0u;
}

static inline void spin_unlock(spin_lock_t *lock, uint32_t save)
{
	(void)lock;
	(void)save;
	if (test_spin_unlock_hook != NULL) {
		test_spin_unlock_hook();
	}
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
