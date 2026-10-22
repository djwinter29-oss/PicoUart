/** @file addressmap.h
 * @brief Host-test SRAM bounds matching the RP2 SDK address maps.
 */
#ifndef HARDWARE_REGS_ADDRESSMAP_H
#define HARDWARE_REGS_ADDRESSMAP_H

#include <stdint.h>

extern uint8_t g_boot_config_flash[4096];
#define XIP_BASE ((uintptr_t)g_boot_config_flash)

/** @brief First physical SRAM address. */
#define SRAM_BASE 0x20000000u
/** @brief End of physical SRAM, excluding XIP cache SRAM. */
#if PICO_RP2350
#define SRAM_END 0x20082000u
#else
#define SRAM_END 0x20042000u
#endif

#endif
