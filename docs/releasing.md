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
   Use `--probe-serial <serial>` when more than one CMSIS-DAP probe is attached
   or when USB enumeration tools are unavailable.
   Use a current OpenOCD CMSIS-DAP build. If OpenOCD reports `Unknown flash
   device` after detecting the SWD target, update OpenOCD and retry with
   `--adapter-speed-khz 1000` before treating the HIL attempt as a firmware
   failure. Flash ID `0x00154068` is a Boya BY25Q16ES device; use an OpenOCD
   build containing that flash-table entry and pass it with `--openocd-exe`.
   Repeat the full matrix below on **both** packaged board images — RP2350 DMA
   COUNT behavior differs from RP2040 and must not be skipped.
3. Run the four staged bridge cases and keep the full console transcript:
   UART0 Debug Probe, HW UART1↔PIO UART2, PIO UART3↔PIO UART4, and UART5
   loopback. Change the jumpers between stages as described in
   `docs/tests/self-test-setup.md`.
4. Run `serial_stress_benchmark.py` at the default rate sweep (or the rates
   claimed in the release notes). Pass `--uart1` with `--uart1-peer <uart2>`
   and `--uart4` with `--uart4-peer <uart3>` when the full staged fixture is
   connected so the benchmark exercises HW1↔PIO2 and PIO3↔PIO4. Record the
   command line, board, clock, duration, verified bytes, and every
   reported stream throughput. A promoted result has no byte mismatch, timeout,
   `rx_overrun`, `rx_error`, or `control_error` in the captured HID monitor.
5. Run rapid line-coding changes on both a hardware UART and a PIO UART while
   the port has queued TX data, while an RX peer is active, and after repeated
   equivalent requests. Verify the captured old-format TX backlog drains before
   the change, unsupported/timeout requests raise `CONTROL_ERROR`, and the
   peer is quiescent before claiming loss-free RX behavior.
6. Run disconnect/remount, watchdog recovery, DMA wrap/re-arm flood, and the
   six-port full-duplex saturation matrix on each board image. HIL must record
   any expected receive loss during a forced format transition.
7. Optionally run the CDC-hold / RX flood step from the board-testing skill
   (`--flood-seconds` / `--hold-cdc-seconds`) when advertising ring/DMA
   backpressure behavior. To claim hardware RTS/CTS, explicitly enable
   `hardware_flow_control` in `firmware/src/config/uart_board.c` first — the
   default build leaves HW flow control off.
   To claim PIO RTS/CTS, enable both PIO flow-control pin flags for a tested
   port and run the PIO CTS hold/release and RTS backpressure procedure in the
   board-testing skill.
8. Attach or link the transcript (and any HID `monitor` snippets showing
   `control_error` / `rx_overrun` expectations) to the GitHub Release notes or a
   linked issue. Cloud CI cannot record HIL; a draft without this attachment is
   lab-only even if USB identity review passed.

A release without a recorded HIL pass is lab-only.

## Promote checklist (draft → published)

Before clicking **Publish** on the GitHub draft:

1. **Artifact ↔ HIL SHA match**: the UF2/ELF/BIN attached to the draft (or their
   `SHA256SUMS-*`) are bit-identical to the images used for the recorded HIL
   pass on **each** board (`pico` and `pico2`). Copy the hashes into the
   [performance result log](tests/performance-test-results.md), then compare
   them against the downloaded release `SHA256SUMS-*` files before promoting.
   Do not promote if HIL ran on a different local rebuild or only one of the two
   targets.
2. **USB identity note**: release notes retain the `0xCAFE:0x4010` lab-project
   identity warning unless the artifact deliberately uses an allocated identity.
3. **HIL transcript** is linked or attached (see above), covering both boards.
4. Release notes call out any breaking HID layout changes.
5. **Python dependency lock**: release CI installed
   `host/python/requirements-lock.txt` with `pip --require-hashes`; any lock
   regeneration is present in the reviewed release change.

## Versioning

Tag form is `vMAJOR.MINOR.PATCH` (no `-rc` / pre-release suffixes for publish).
Major and minor must be `0-99`; patch must be `0-255`.
The tag stamps HID firmware version `MAJOR.MINOR.PATCH` and USB `bcdDevice` as
major.minor BCD only (for example `v1.2.3` → HID `1.2.3`, `bcdDevice` `0x0102`).
Details are in the root README.

Release and PR workflows build against Pico SDK 2.3.0 at commit
`98a542c1a62fb549ffb5d66a3e5892b06276b670` and print the verified revision in
the job log.

Python release qualification uses the reviewed, fully transitive
`host/python/requirements-lock.txt` with artifact hashes. Regenerate it only in
a packaging-enabled, reviewed change; the pinned direct requirements remain the
human-edited inputs rather than the release installation source.
