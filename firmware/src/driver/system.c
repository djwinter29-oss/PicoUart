/**
 * @file system.c
 * @brief Board-level system-clock setup for PicoUart firmware.
 */

#include "driver/system.h"

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

/**
 * @brief Raise core voltage before an above-rated system clock.
 *
 * Rated clocks stay at the power-up 1.10 V. RP2040 is rated through 133 MHz
 * and RP2350 through 150 MHz. Above that, `set_sys_clock_khz` alone leaves
 * the core at 1.10 V, which is not enough for a stable 200 MHz UART clock.
 * The supported overclock voltage ceiling is 1.30 V (the SDK default maximum).
 * Any clock target above the rated frequency, including the development RP2350
 * 500 MHz image, is unqualified until validated on the exact board across the
 * intended temperature and workload range. Do not disable the SDK voltage
 * limit as a workaround; higher-voltage operation requires separate hardware
 * analysis and explicit qualification.
 * @param clock_khz Requested `clk_sys` in kHz.
 */
static void system_raise_voltage_for_clock(uint32_t clock_khz)
{
#if defined(PICO_RP2350A) || defined(PICO_RP2350B)
    const uint32_t rated_khz = 150000u;
#else
    const uint32_t rated_khz = 133000u;
#endif
    enum vreg_voltage voltage;

    if (clock_khz > 250000u) {
        voltage = VREG_VOLTAGE_1_30;
    } else if (clock_khz > 200000u) {
        voltage = VREG_VOLTAGE_1_25;
    } else if (clock_khz > rated_khz) {
        voltage = VREG_VOLTAGE_1_15;
    } else {
        return;
    }

    vreg_set_voltage(voltage);
    /* The regulator needs time to settle before the PLL steps up. */
    sleep_ms(1);
}

/** @copydoc system_init_clock */
void system_init_clock(void)
{
    system_raise_voltage_for_clock(PICO_UART_SYSTEM_CLOCK_KHZ);
    hard_assert(set_sys_clock_khz(PICO_UART_SYSTEM_CLOCK_KHZ, true));
}

/** @copydoc system_watchdog_enable */
void system_watchdog_enable(void)
{
    watchdog_enable(PICO_UART_WATCHDOG_TIMEOUT_MS, true);
}

/** @copydoc system_watchdog_update */
void system_watchdog_update(void)
{
    watchdog_update();
}

/**
 * @brief Halt after a HardFault so a debugger can inspect the fault.
 *
 * The USB poll loop arms the watchdog with pause-on-debug. A debugger keeps
 * the core stopped here; an unattended board resets when the watchdog expires.
 */
void isr_hardfault(void)
{
    while (true) {
        tight_loop_contents();
    }
}

/** @copydoc system_reset */
void system_reset(void)
{
    watchdog_reboot(0u, 0u, 0u);
}