#include "pico/stdlib.h"

#include "driver/system.h"
#include "driver/uart_driver.h"
#include "usb/usb_cdc.h"
#include "usb/usb_hid.h"

int main(void)
{
    system_init_clock();
    usb_cdc_init();
    usb_hid_init();
    hard_assert(uart_driver_validate_topology());
    hard_assert(uart_driver_init());

    while (true) {
        uart_driver_poll_hardware();
        uart_driver_poll_pio();
        usb_cdc_poll();
        usb_hid_poll();
        tight_loop_contents();
    }
}