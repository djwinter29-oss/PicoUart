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

Follow the USB-identity and recorded-HIL gates in
[`docs/releasing.md`](docs/releasing.md):

1. Obtain an allocated VID/PID (pid.codes or a commercial USB-IF vendor ID).
2. Update `PICO_UART_USB_VID` / `PICO_UART_USB_PID` in `usb_identity.h`.
3. Keep [`host/python/src/pico_uart_hid.py`](host/python/src/pico_uart_hid.py) in sync.
4. Note the identity change in release notes (CI derives the USB needle from `usb_identity.h`).

## HID board controls

The vendor HID interface accepts commands that toggle the board LED and can
reset the device. Reset requires a two-step sequence (arm, then reset within
2 seconds). Any local user who can open the HID node can still reboot the board
when HID reset is compiled in.

Mitigations:

- Prefer udev/`dialout` ACLs that limit HID access on shared hosts.
- Build with `-DPICO_UART_ALLOW_HID_RESET=0` to disable remote reset entirely.

## Reporting issues

Open a GitHub issue for security-relevant findings. There is no separate
private disclosure channel yet.
