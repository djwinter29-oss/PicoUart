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

/**
 * @brief Configure the system clock before initializing timing-sensitive hardware.
 */
void system_init_clock(void);

/**
 * @brief Reboot the board immediately through the watchdog.
 */
void system_reset(void);

#endif