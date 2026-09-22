# Releasing PicoUart

Release tags matching `vMAJOR.MINOR.PATCH` run
[`.github/workflows/release.yml`](../.github/workflows/release.yml). The workflow
builds both board targets, runs host tests, packages UF2/ELF/BIN/HEX plus
`SHA256SUMS-*`, and opens a **draft** GitHub Release.

Do not publish the draft until exact-artifact hardware-in-the-loop (HIL) evidence
passes the gates below.

Suggested flow:

1. Run a `workflow_dispatch` dry-run.
2. Download those artifacts and run HIL on the exact files.
3. Tag `vMAJOR.MINOR.PATCH`.
4. Compare draft artifacts with the HIL hashes.
5. Publish only after the promote checklist passes.

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

## Release HIL Gates

Cloud CI proves builds and host tests only. A publishable release needs recorded
HIL on both packaged board images:

- Raspberry Pi Pico / RP2040 (`pico`)
- Raspberry Pi Pico 2 / RP2350 (`pico2`)

HIL must use the exact UF2/ELF from the draft release or workflow dry-run. Do
not rebuild locally for release qualification.

This gate is intentionally manual: the repository has no CI-attached Pico,
Debug Probe, USB cable, or jumper fixture. Automated workflows must not mark a
release as physically qualified without linked human-run HIL evidence.

Record all of the following in
[Performance Test Results](tests/performance-test-results.md) or a linked raw
transcript:

- board target and physical board used
- artifact path/name and SHA-256
- firmware version and commit
- command lines
- verified bytes and throughput per link
- HID health before and after tests
- expected `control_error`, `rx_overrun`, or receive-loss notes when applicable

A release without this recorded evidence is lab-only.

## Flashing Release Artifacts

Flash with the packaged ELF/UF2 and skip rebuilding:

```sh
tools/firmware/load.sh --board <pico|pico2> --skip-build --elf <path-to-release.elf>
```

Use `--probe-serial <serial>` when more than one CMSIS-DAP probe is attached or
when USB enumeration tools are unavailable.

Use a current OpenOCD CMSIS-DAP build. If OpenOCD reports `Unknown flash device`
after detecting the SWD target, update OpenOCD and retry with
`--adapter-speed-khz 1000` before treating the HIL attempt as firmware failure.
Flash ID `0x00154068` is a Boya BY25Q16ES device; older OpenOCD builds may need
an upstream binary selected with `--openocd-exe`.

## Required HIL Matrix

Follow [`.github/skills/pico-uart-board-testing/SKILL.md`](../.github/skills/pico-uart-board-testing/SKILL.md)
and [Self-Test Setup](tests/self-test-setup.md). Install the complete fixed
fixture before starting and do not rewire during the run.

Run these gates on both board targets:

1. Four staged bridge cases:
   - UART0 Debug Probe
   - HW UART1 to PIO UART2
   - PIO UART3 to PIO UART4
   - UART5 loopback
2. Concurrent performance benchmark using the full staged fixture. Pass
   `--uart1 --uart1-peer <uart2> --uart4 --uart4-peer <uart3>` so the benchmark
   exercises HW1 to PIO2 and PIO3 to PIO4.
3. Rapid line-coding changes on one hardware UART and one PIO UART while queued
   TX data drains and an RX peer is active.
4. Disconnect/remount, watchdog recovery, DMA wrap/re-arm flood, and six-port
   full-duplex saturation checks.

A promoted result has no unexplained byte mismatch, timeout, USB disconnect,
`rx_error`, `rx_overrun`, or `control_error`.

## Optional Claims

Run optional tests only when the release notes claim the behavior:

- CDC-hold / RX flood backpressure (`--flood-seconds` /
  `--hold-cdc-seconds`).
- Hardware RTS/CTS after enabling `hardware_flow_control` in
  `firmware/src/config/uart_board.c`.
- PIO RTS/CTS after enabling the selected PIO flow-control pin flags and running
  CTS hold/release plus RTS backpressure checks.

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
Major and minor must be `0-99`; patch must be `0-255`. This range is a
**release-tag policy** (enforced by the `Resolve version` step in
`release.yml`), not a firmware build limit: local/manual builds via
`tools/firmware/build.sh --firmware-version ...` accept major, minor, and patch each up
to `255`, and USB `bcdDevice` falls back to `0x0000` once major or minor
exceeds `99` (see `firmware/CMakeLists.txt`).
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
