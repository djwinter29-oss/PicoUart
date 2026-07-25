# Security Policy

## USB identity

PicoUart currently enumerates as USB **`cafe:4010`**. These IDs are a
**development / lab placeholder** defined in
[`firmware/src/config/usb_identity.h`](firmware/src/config/usb_identity.h).
They are not an allocated USB-IF or [pid.codes](https://pid.codes) identity.

Do **not** ship production devices with the placeholder IDs:

- Other projects may collide on the same VID/PID.
- OS driver/association behavior is undefined across machines.
- Changing the identity later is a breaking USB change for existing installs.

### Before a public or production release

1. Obtain an allocated VID/PID (pid.codes or a commercial USB-IF vendor ID).
2. Update `PICO_UART_USB_VID` / `PICO_UART_USB_PID` in `usb_identity.h`.
3. Keep [`host/python/src/pico_uart_hid.py`](host/python/src/pico_uart_hid.py) in sync.
4. Note the identity change in release notes.

## HID board controls

The vendor HID interface accepts commands that toggle the board LED and
**immediately reset** the device. Any local user who can open the HID node can
reboot the board. Prefer udev/`dialout` ACLs that limit access on shared hosts.

## Reporting issues

Open a GitHub issue for security-relevant findings. There is no separate
private disclosure channel yet.
