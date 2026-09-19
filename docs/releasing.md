# Releasing PicoUart

Release tags matching `vMAJOR.MINOR.PATCH` (plain semver only) run
[`.github/workflows/release.yml`](../.github/workflows/release.yml). That
workflow builds both boards, runs host unit tests, packages UF2/ELF/BIN/HEX plus
SHA256SUMS, and opens a **draft** GitHub Release. Promote the draft only after
this checklist passes.

Suggested flow: `workflow_dispatch` dry-run → HIL on those artifacts → tag →
review draft → publish.

## USB identity

PicoUart publishes lab and test artifacts using `cafe:4010`, an unallocated
development USB identity. This project is not a commercial product, so the
release workflow deliberately permits this identity and does not gate tag
releases on a VID/PID allocation.

Do not reuse these IDs for a commercial device: another project may collide on
the same identity and operating-system driver association is undefined. A
commercial derivative must obtain its own VID/PID and update
[`firmware/src/config/usb_identity.h`](../firmware/src/config/usb_identity.h)
and [`host/python/src/pico_uart_hid.py`](../host/python/src/pico_uart_hid.py).
See also [`SECURITY.md`](../SECURITY.md).

## Recorded HIL pass (gate)

Cloud / CI builds prove compilation and host unit tests only. Release candidates
need a recorded hardware-in-the-loop (HIL) pass:

1. Follow [`.github/skills/pico-uart-board-testing/SKILL.md`](../.github/skills/pico-uart-board-testing/SKILL.md)
   and [`docs/test-connections.md`](test-connections.md).
2. Flash the **exact** UF2/ELF attached to the GitHub Release (or the
   `workflow_dispatch` dry-run artifacts). Do **not** rebuild for release HIL.
   Record `SHA256SUMS-*` and flash with
   `tools/linux/load.sh --board <pico|pico2> --skip-build --elf <path-to-release.elf>`.
   Repeat the full matrix below on **both** packaged board images — RP2350 DMA
   COUNT behavior differs from RP2040 and must not be skipped.
3. Run the required bridge cases and keep the full console transcript:
   UART0 Debug Probe, UART2↔UART3 cross, and UART5 loopback. Include UART1
   and UART4 loopbacks when those jumpers are fitted (optional for promote;
   recommended when claiming six-port coverage).
4. Run `serial_stress_benchmark.py` at the default rate sweep (or the rates
   claimed in the release notes). Pass `--uart1` / `--uart4` only when those
   jumpers are fitted.
5. Optionally run the CDC-hold / RX flood step from the board-testing skill
   (`--flood-seconds` / `--hold-cdc-seconds`) when advertising ring/DMA
   backpressure behavior. To claim hardware RTS/CTS, explicitly enable
   `hardware_flow_control` in `firmware/src/config/uart_board.c` first — the
   default build leaves HW flow control off.
   To claim PIO RTS/CTS, enable both PIO flow-control pin flags for a tested
   port and run the PIO CTS hold/release and RTS backpressure procedure in the
   board-testing skill.
6. Attach or link the transcript (and any HID `monitor` snippets showing
   `control_error` / `rx_overrun` expectations) to the GitHub Release notes or a
   linked issue. Cloud CI cannot record HIL; a draft without this attachment is
   lab-only even if the USB-identity gate passed.

A release without a recorded HIL pass is lab-only.

## Promote checklist (draft → published)

Before clicking **Publish** on the GitHub draft:

1. **Artifact ↔ HIL SHA match**: the UF2/ELF/BIN attached to the draft (or their
   `SHA256SUMS-*`) are bit-identical to the images used for the recorded HIL
   pass on **each** board (`pico` and `pico2`). Do not promote if HIL ran on a
   different local rebuild or only one of the two targets.
2. **USB identity note**: release notes retain the `0xCAFE:0x4010` lab-project
   identity warning unless the artifact deliberately uses an allocated identity.
3. **HIL transcript** is linked or attached (see above), covering both boards.
4. Release notes call out any breaking HID layout changes.

## Versioning

Tag form is `vMAJOR.MINOR.PATCH` (no `-rc` / pre-release suffixes for publish).
Major and minor must be `0-99`; patch must be `0-255`.
The tag stamps HID firmware version `MAJOR.MINOR.PATCH` and USB `bcdDevice` as
major.minor BCD only (for example `v1.2.3` → HID `1.2.3`, `bcdDevice` `0x0102`).
Details are in the root README.

Release and PR workflows build against Pico SDK 2.2.0 at commit
`a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` and print the verified revision in
the job log.
