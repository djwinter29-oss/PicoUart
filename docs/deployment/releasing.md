# Releasing PicoUart

A release has two independent halves:

1. **Publication** is automatic. Pushing a version tag builds, tests, and publishes the firmware and host packages.
2. **Hardware qualification** is manual. Someone flashes the exact published firmware and runs the HIL fixture.

A green CI run shows that the software checks passed; it says nothing about UART timing, reliability, or board
stability. Until qualification evidence exists, treat published artifacts as lab/test-only.

## Release Sequence

1. Complete the [preflight](#release-preflight).
2. Push the `vMAJOR.MINOR.PATCH` tag. The Release workflow publishes the GitHub Release once its gates pass, and the
   PyPI and Pages publishers follow automatically ([host app publishing](host-app-publishing.md)).
3. [Flash the packaged images](#flashing-release-artifacts) without rebuilding, and collect the
   [required HIL evidence](#required-hil-matrix) for each rated board.
4. Work through the [promotion checklist](#promotion-checklist) before calling the release hardware-qualified.

The CI job graph is described in [GitHub Actions](../development/github-actions.md).

## Release Preflight

Before tagging, make sure the change is reviewed, the tree has no stray files, `git diff --check` is clean, and PR
Check passed for the exact commit you are tagging. Pick the version using the [versioning policy](#versioning), and
mention any USB/HID compatibility or behavior change in the release notes. Also confirm private vulnerability
reporting is enabled in the repository settings ([SECURITY.md](../../SECURITY.md)).

Don't start physical testing from an uncommitted or locally modified image; the result would not describe anything you
can release.

## Publication Gates

If any gate is missing, nothing is published. The GitHub Release job waits for host validation, both firmware builds,
both host packages, and a successful **PR Check** run on the same commit. A tag push does not trigger PR Check itself,
so make sure the commit already has one (usually from the push to `main`). PyPI and Pages additionally require a
successful tag-push Release run for that commit and tag.

The [gate script](../../tools/release/require_release_checks.py) looks at the latest matching run. A newer failed or
pending run blocks publication even if an older run succeeded. Publishers also refuse a tag that has moved away from
the triggering commit.

Manual test modes are the only exception. They run their own scoped checks, use clearly marked non-release versions,
and never qualify firmware; see [host app publishing](host-app-publishing.md#manual-test-modes).

## USB Identity

PicoUart uses the Raspberry Pi-allocated identity `2e8a:116b` from the
[official PID list](https://github.com/raspberrypi/usb-pid). Do not reuse it for other products; derivatives need their
own VID/PID. Changing the identity is a breaking USB change.

## Flashing Release Artifacts

Stop UART traffic and close host applications first. Use the packaged artifact for the target board and check its
SHA-256 against the release checksum manifest. Both methods work for first installs and upgrades.

### USB Drive (UF2)

No debug probe or build tools are needed.

1. Download the `.uf2` for your board: `pico` for Raspberry Pi Pico, `pico2` for Pico 2.
2. Hold **BOOTSEL** while connecting the USB cable. If the board is already powered, unplug it first.
3. Release BOOTSEL when the bootloader drive appears, and copy the `.uf2` onto it.
4. The drive disappears when the copy finishes, the board reboots, and the six serial ports and HID interface appear.
   Check the reported firmware version before resuming UART traffic.

On boards with a **RESET** button you can instead hold BOOTSEL, tap RESET, then release BOOTSEL. Standard Pico and
Pico 2 boards have no reset button.

### Debug Probe (ELF)

Programming over SWD flashes the packaged ELF without rebuilding it:

```sh
tools/firmware/load.sh --board pico --skip-build --elf /path/to/release-pico.elf
```

Use `--board pico2` with the Pico 2 ELF. Add `--probe-serial <serial>` when more than one CMSIS-DAP probe is attached.
SWD defaults to 1000 kHz; use `--adapter-speed-khz 100` for marginal wiring. Without `--skip-build`, the loader does an
incremental development build first.

<details>
<summary>Probe permissions, unknown flash devices, and unstable SWD</summary>

On Linux, `unable to find a matching CMSIS-DAP device` often means USB access was denied, even if `lsusb` lists the
probe. The loader retries with noninteractive sudo when it can; otherwise add a udev rule for the probe. To test probe
access without touching flash:

```sh
openocd -d3 -f interface/cmsis-dap.cfg -c 'transport select swd' -c 'adapter speed 1000' -c 'init; shutdown'
```

If OpenOCD reports `Unknown flash device` after finding the target, update OpenOCD before blaming the firmware. Flash
ID `0x00154068` (Boya BY25Q16ES) needs a recent build; pass one with `--openocd-exe`.

One recovery that worked in a recorded development session was to disconnect both the board and probe, wait for their
LEDs to go off, then reconnect. The record notes that retries and programming only core 0 had not restored verified
programming; treat this as an observation, not a confirmed fix for every SWD failure
([review record](../tests/records/2026-10-23-uart-cdc-review-validation.md#cause-and-retained-change)).

</details>

### Override QSPI Auto-Detection (Advanced)

<details>
<summary>Only with stable SWD and a flash capacity confirmed from the chip marking and datasheet</summary>

OpenOCD uses the flash capacity to set erase bounds, so a guessed value can make flashing unsafe. Prefer a current
OpenOCD with normal detection. When that is not possible, the RP2040/RP2350 target scripts accept a `FLASHSIZE` in
bytes:

```sh
: "${FLASH_SIZE_BYTES:?Set the flash capacity in bytes from the verified chip marking and datasheet}"
OPENOCD_TARGET_CFG="$(mktemp)"
trap 'rm -f "$OPENOCD_TARGET_CFG"' EXIT
printf 'set FLASHSIZE %s\nsource [find target/rp2040.cfg]\n' "$FLASH_SIZE_BYTES" > "$OPENOCD_TARGET_CFG"
tools/firmware/load.sh --board pico --skip-build --elf /path/to/pico.elf \
   --openocd-target "$OPENOCD_TARGET_CFG" --probe-serial "<probe-serial>" --adapter-speed-khz 1000
```

For Pico 2, use `--board pico2`, a Pico 2 ELF, and `target/rp2350.cfg`. Continue only if OpenOCD reports
**Verified OK**. The override skips identification only; it does not fix SWD wiring or reset problems.

</details>

## Release HIL Gates

A release is hardware-qualified only when every rated board has a complete `PASS` for the required HIL matrix.

Record the reported system clock and boot clock profile alongside the image hash, since the clock profile is stored
outside the firmware image. Overclock profiles are board-local, so an overclock claim needs its own evidence on that
board.

The [fixture setup](../tests/hil-fixture-setup.md) and [test plan](../tests/hil-fixture-test-plan.md) define how to
run the tests. Keep one record per board in the [HIL records](../tests/records/README.md) format.

## Required HIL Matrix

| Coverage | Required evidence |
| --- | --- |
| Functional fixture | Both crossed pairs in both directions and both loopbacks; all six CDC ports and HID enumerate |
| Performance | Pair and concurrent payload integrity at declared rates and durations, with clean HID health |
| Stability | Repeated or soak runs at the declared workload, with duration and conditions recorded |
| Control and recovery | Rapid line-coding changes, disconnect/remount, watchdog recovery, and DMA wrap/re-arm |
| Artifact identity | Board target, firmware and runner revisions, and the tested artifact's SHA-256 |

Missing cases make the result `PARTIAL`. Unexplained loss, timeouts, disconnects, or HID errors block qualification.
A speed-sweep `PASS` qualifies only the 115200-baud baseline. The boundary it finds is a measurement, and higher
rates remain unrated. Older [record index](../tests/records/README.md#evidence-index) entries cannot
substitute for a current exact-artifact run.

Optional claims, such as RTS/CTS or backpressure behavior, need their matching board configuration and HIL variant
before the release notes may make them.

## Promotion Checklist

Before calling the release qualified, confirm:

- All build gates passed for the exact commit.
- Every rated firmware artifact has complete passing evidence with matching hashes.
- Records and transcripts are in a shared location, not only in a local cache.
- Release notes state compatibility changes, tested conditions, and claims that remain unqualified.

Attach the evidence and note hardware qualification in the release notes.

## Versioning

Every release-version input is called **tag** and takes `vMAJOR.MINOR.PATCH` (for example `v1.2.3`); unprefixed
versions and pre-release suffixes are rejected. Firmware, the Python wheel, and the .NET host all report the same
unprefixed version, and packaging checks verify that they match. Firmware reports it over HID and puts major/minor in
USB `bcdDevice`.

For a manual Release preflight or .NET packaging run, **tag** is only a version label: it does not need to exist and is
not created. Stable PyPI uploads and release-mode Pages deployments require an existing published release tag.
