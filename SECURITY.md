# Security Policy

## USB identity

PicoUart currently enumerates as USB **`cafe:4010`**. These IDs are a
**development / lab placeholder** defined in
[`firmware/src/config/usb_identity.h`](firmware/src/config/usb_identity.h).
They are not an allocated USB-IF or [pid.codes](https://pid.codes) identity.

PicoUart releases are lab/project artifacts, so release CI permits this identity.
See [`docs/releasing.md`](docs/releasing.md). Commercial derivatives must use an
allocated identity.

Do **not** ship production devices with the placeholder IDs:

- Other projects may collide on the same VID/PID.
- OS driver/association behavior is undefined across machines.
- Changing the identity later is a breaking USB change for existing installs.

### Commercial derivatives

Before shipping a commercial derivative:

1. Obtain an allocated VID/PID (pid.codes or a commercial USB-IF vendor ID).
2. Update `PICO_UART_USB_VID` / `PICO_UART_USB_PID` in `usb_identity.h`.
3. Keep [`host/python/src/pico_uart_hid.py`](host/python/src/pico_uart_hid.py) in sync.
4. Note the identity change in release notes (CI derives the USB needle from `usb_identity.h`).

## HID board controls

The vendor HID interface accepts commands that toggle the board LED. Remote
reset is disabled by default. Trusted lab builds may enable it with
`-DPICO_UART_ALLOW_HID_RESET=1`; reset then requires a two-step sequence (arm,
then reset within 2 seconds). Enabled builds advertise that capability in HID
board-status `reserved0` bit 0; the reference host tool refuses `reset` when
the bit is clear. Any local user who can open the HID node can still reboot a
build that enables it.

Mitigations:

- Keep the default `PICO_UART_ALLOW_HID_RESET=0` for shared or production hosts.
- Prefer udev/`dialout` ACLs that limit HID access when reset is enabled.

## Reporting issues

Enable private vulnerability reporting in the repository's GitHub **Settings →
Security → Code security and analysis** before publishing a release. Once
enabled, report vulnerabilities through the repository's **Security → Advisories
→ Report a vulnerability** flow; maintainers must acknowledge the report before
requesting any public GitHub issue or CVE disclosure.

Do not include exploit details in a public issue while a private advisory is
open. If private reporting is unavailable, maintainers must enable it rather
than asking the reporter to disclose a vulnerability publicly.
