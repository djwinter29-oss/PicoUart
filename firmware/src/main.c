/**
 * @file main.c
 * @brief PicoUart firmware startup and cooperative service loop.
 */

#include "pico/stdlib.h"

#include "driver/led.h"
#include "driver/system.h"
#include "driver/temperature.h"
#include "uart/ring_buffer/ring_buffer.h"
#include "uart/uart_driver.h"
#include "usb/usb_cdc.h"
#include "usb/usb_hid.h"

/**
 * @brief Initialize the board and run the USB/UART bridge forever.
 *
 * The watchdog is armed before clock and peripheral setup so initialization
 * stalls recover automatically. Startup then initializes board services,
 * validates the ring-buffer and UART topology contracts, starts the UART
 * worker, and finally initializes the USB CDC/HID services. A failed
 * unrecoverable startup check intentionally halts until the watchdog resets
 * the board. During normal operation, the watchdog is fed only while the
 * UART worker heartbeat remains fresh.
 *
 * @return Never returns.
 */
int main(void)
{
    system_watchdog_enable();
    system_init_clock();
    led_init();
    temperature_init();

    /* Validate shared invariants before starting UART and USB services. */
    hard_assert(ring_buffer_self_check());
    hard_assert(uart_driver_validate_topology());
    hard_assert(uart_driver_init());
    usb_cdc_init();
    usb_hid_init();

    while (true) {
        usb_cdc_poll();
        usb_hid_poll();
        if (uart_driver_worker_heartbeat_is_fresh()) {
            system_watchdog_update();
        }
        tight_loop_contents();
    }
}
