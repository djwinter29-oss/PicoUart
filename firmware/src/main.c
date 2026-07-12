#include "pico/stdlib.h"

#include "driver/uart_driver.h"
#include "usb/usb_cdc.h"
#include "usb/usb_hid.h"

int main(void)
{
    hard_assert(uart_driver_validate_topology());
    usb_cdc_init();
    usb_hid_init();

    while (true) {
        usb_cdc_poll();
        usb_hid_poll();
        tight_loop_contents();
    }
}