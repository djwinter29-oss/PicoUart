#include "pico/stdlib.h"

#include "driver/led.h"
#include "driver/system.h"
#include "driver/temperature.h"
#include "uart/ring_buffer/ring_buffer.h"
#include "uart/uart_driver.h"
#include "usb/usb_cdc.h"
#include "usb/usb_hid.h"

int main(void)
{
    system_init_clock();
    led_init();
    temperature_init();
    usb_cdc_init();
    usb_hid_init();
    hard_assert(ring_buffer_self_check());
    hard_assert(uart_driver_validate_topology());
    hard_assert(uart_driver_init());

    while (true) {
        usb_cdc_poll();
        usb_hid_poll();
        tight_loop_contents();
    }
}