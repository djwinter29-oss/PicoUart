/**
 * @file service_load.h
 * @brief Per-core wall-time attribution to service passes that handle work.
 */
#ifndef SERVICE_LOAD_H
#define SERVICE_LOAD_H

#include <stdint.h>

/** @brief Unavailable until a core completes its first measurement window. */
#define SERVICE_LOAD_UNAVAILABLE UINT16_MAX

/** @brief Enable intrusive SWD-readable timing diagnostics only in dedicated profiling builds. */
#ifndef PICO_UART_PROFILE_TIMING
#define PICO_UART_PROFILE_TIMING 0
#endif

#if PICO_UART_PROFILE_TIMING
/** @brief Maximum observed service-pass duration per core since boot, in microseconds. */
extern volatile uint32_t service_load_max_pass_us[2];
/** @brief Maximum observed interval between service-pass starts per core, in microseconds. */
extern volatile uint32_t service_load_max_gap_us[2];
#endif

/** @brief Reset both core windows before launching core 1. */
void service_load_init(void);

/**
 * @brief Start one service pass on its owning core.
 * @param core Core index (0 or 1); only that core may update its window.
 * @param now_us Low 32 bits of the monotonic microsecond timer.
 */
void service_load_begin(unsigned int core, uint32_t now_us);

/**
 * @brief Mark the current pass as handling data, USB events, or control work.
 * @param core Owning core index (0 or 1), called from service context, not an ISR.
 */
void service_load_note(unsigned int core);

/**
 * @brief Charge marked passes and publish a ratio after at least one second.
 * @param core Owning core index (0 or 1).
 * @param now_us Low 32 bits of the monotonic microsecond timer.
 */
void service_load_end(unsigned int core, uint32_t now_us);

/**
 * @brief Read the last completed window safely from either core.
 * @param core Measured core index (0 or 1).
 * @return Service wall-time basis points (0..10000), or SERVICE_LOAD_UNAVAILABLE.
 */
uint16_t service_load_read(unsigned int core);

#endif
