# Releasing PicoUart

Release tags matching `vMAJOR.MINOR.PATCH` (plain semver only) run
[`.github/workflows/release.yml`](../.github/workflows/release.yml). That
workflow builds both boards, runs host unit tests, packages UF2/ELF/BIN/HEX plus
SHA256SUMS, and opens a **draft** GitHub Release. Promote the draft only after
this checklist passes.

Suggested flow: `workflow_dispatch` dry-run → HIL on those artifacts → tag →
review draft → publish.

## USB identity (gate)

Lab images may keep the development placeholder `cafe:4010`. **Do not** ship a
public or production release on that identity.

Before promoting a non-lab draft:

1. Obtain an allocated VID/PID (pid.codes or a commercial USB-IF vendor ID).
2. Update `PICO_UART_USB_VID` / `PICO_UART_USB_PID` in
   [`firmware/src/config/usb_identity.h`](../firmware/src/config/usb_identity.h).
3. Keep [`host/python/src/pico_uart_hid.py`](../host/python/src/pico_uart_hid.py)
   in sync (CI parses the header defines; no manual needle edits in
   `release.yml`).
4. Call out the identity change in release notes as a breaking USB change.

See also [`SECURITY.md`](../SECURITY.md).

## Recorded HIL pass (gate)

Cloud / CI builds prove compilation and host unit tests only. Release candidates
need a recorded hardware-in-the-loop (HIL) pass:

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

## Promote checklist (draft → published)

Before clicking **Publish** on the GitHub draft:

1. **Artifact ↔ HIL SHA match**: the UF2/ELF/BIN attached to the draft (or their
   `SHA256SUMS-*`) are bit-identical to the images used for the recorded HIL
   pass. Do not promote if HIL ran on a different local rebuild.
2. **USB identity**: for non-lab releases, `PICO_UART_USB_VID` /
   `PICO_UART_USB_PID` are **not** the development placeholder `0xCAFE` /
   `0x4010` (confirm in release notes and in the packaged binaries).
3. **HIL transcript** is linked or attached (see above).
4. Release notes call out any breaking USB identity or HID layout changes.

## Versioning

Tag form is `vMAJOR.MINOR.PATCH` (no `-rc` / pre-release suffixes for publish).
The tag stamps HID firmware version `MAJOR.MINOR.PATCH` and USB `bcdDevice` as
major.minor BCD only (for example `v1.2.3` → HID `1.2.3`, `bcdDevice` `0x0102`).
Details are in the root README.
