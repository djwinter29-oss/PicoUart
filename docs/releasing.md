# Releasing PicoUart

Release tags matching `v*` build firmware artifacts through
[`.github/workflows/release.yml`](../.github/workflows/release.yml). Use this
checklist before publishing a non-lab / production-facing release.

## USB identity (P1 gate)

Lab images may keep the development placeholder `cafe:4010`. **Do not** ship a
public or production release on that identity.

Before tagging a non-lab release:

1. Obtain an allocated VID/PID (pid.codes or a commercial USB-IF vendor ID).
2. Update `PICO_UART_USB_VID` / `PICO_UART_USB_PID` in
   [`firmware/src/config/usb_identity.h`](../firmware/src/config/usb_identity.h).
3. Keep [`host/python/src/pico_uart_hid.py`](../host/python/src/pico_uart_hid.py)
   in sync.
4. Update the `cafe:4010` assertion in `release.yml` (or replace it with the new
   little-endian VID/PID needle) so CI matches the shipped identity.
5. Call out the identity change in release notes as a breaking USB change.

See also [`SECURITY.md`](../SECURITY.md).

## Recorded HIL pass (P1 gate)

Cloud / CI builds prove compilation only. Release candidates need a recorded
hardware-in-the-loop (HIL) pass:

1. Follow [`.github/skills/pico-uart-board-testing/SKILL.md`](../.github/skills/pico-uart-board-testing/SKILL.md)
   and [`docs/test-connections.md`](test-connections.md).
2. Flash the exact UF2/ELF that will be attached to the GitHub Release.
3. Run the UART0 Debug Probe, UART2↔UART3 cross, and UART5 loopback
   `serial_bridge_test.py` cases; keep the full console transcript.
4. Run `serial_stress_benchmark.py` at the default rate sweep (or the rates
   claimed in the release notes).
5. Optionally run the RTS-ignoring HW UART RX stress step from the board-testing
   skill when advertising flow-control behavior.
6. Attach or link the transcript (and any HID `monitor` snippets showing
   `control_error` / `rx_overrun` expectations) to the GitHub Release notes or a
   linked issue.

A release without a recorded HIL pass is lab-only.

## Versioning

Tag form is `vMAJOR.MINOR.PATCH`. The tag stamps HID firmware version
`MAJOR.MINOR.PATCH` and USB `bcdDevice` as major.minor BCD only (for example
`v1.2.3` → HID `1.2.3`, `bcdDevice` `0x0102`). Details are in the root README.
