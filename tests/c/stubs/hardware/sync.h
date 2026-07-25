/**
 * @file sync.h
 * @brief Host-test stub for Pico SDK hardware/sync.h.
 *
 * Only the barrier used by ring_buffer is required on the host toolchain.
 */

#ifndef HARDWARE_SYNC_H
#define HARDWARE_SYNC_H

static inline void __dmb(void)
{
}

#endif
