/**
 * @file system.h
 * @brief Board-level system-clock setup for PicoUart firmware.
 */

#ifndef SYSTEM_H
#define SYSTEM_H

/** @brief SDK-standard system-clock target in kHz for RP2040 boards. */
#define PICO_UART_RP2040_SYSTEM_CLOCK_KHZ 125000u
/** @brief SDK-standard system-clock target in kHz for RP2350 boards. */
#define PICO_UART_RP2350_SYSTEM_CLOCK_KHZ 150000u

/** @brief Configured system-clock target in kHz. */
#ifndef PICO_UART_SYSTEM_CLOCK_KHZ
#if defined(PICO_RP2350A) || defined(PICO_RP2350B)
#define PICO_UART_SYSTEM_CLOCK_KHZ PICO_UART_RP2350_SYSTEM_CLOCK_KHZ
#else
#define PICO_UART_SYSTEM_CLOCK_KHZ PICO_UART_RP2040_SYSTEM_CLOCK_KHZ
#endif
#endif

/** @brief Watchdog timeout that covers USB poll plus a 1 s line-coding apply. */
#ifndef PICO_UART_WATCHDOG_TIMEOUT_MS
#define PICO_UART_WATCHDOG_TIMEOUT_MS 8000u
#endif

/**
 * @brief Configure the system clock before initializing timing-sensitive hardware.
 *
 * Calls `set_sys_clock_khz` only; the core voltage is never written, preserving
 * the regulator setting on entry rather than restoring a specific voltage. Above
 * the chip's rated clock (133 MHz on RP2040, 150 MHz on RP2350) this is an
 * unqualified overclock at stock voltage: the RP2040 250 MHz and RP2350
 * 300 MHz development images require exact-board HIL across the intended
 * operating conditions before any stability claim.
 */
void system_init_clock(void);

/**
 * @brief Arm the watchdog so a wedged USB poll loop recovers without a power cycle.
 *
 * Pauses the watchdog while a debugger is attached so HardFault can still be
 * inspected. Without a debugger, `isr_hardfault` spins until this timeout resets.
 */
void system_watchdog_enable(void);

/**
 * @brief Pet the watchdog from the USB poll loop when the UART worker is alive.
 */
void system_watchdog_update(void);

/**
 * @brief Reboot the board immediately through the watchdog.
 */
void system_reset(void);

#endif