# Releasing PicoUart

Build the release artifacts, test the exact packaged firmware on each rated board, then publish the qualified draft.
This checklist covers that path; it does not treat a successful CI run as hardware qualification.

> [!IMPORTANT]
> Keep the release as a draft until all required HIL evidence passes and artifact hashes match. A local rebuild or a
> passing result for another image does not qualify the files being published.

For the check graph and tool dependencies, see
[GitHub Actions: Validation and Releases](../development/github-actions.md).
For downloads and publisher operations, see [Host App Distribution](host-app-distribution.md) and
[Host App Publishing](host-app-publishing.md).

## Release Sequence

1. Complete [preflight](#release-preflight) and confirm the [version policy](#versioning).
2. Run a manual preflight if needed, or push the release tag to build the gated draft and checksum manifests.
3. [Flash the packaged images](#flashing-release-artifacts), without rebuilding, and collect the
   [required HIL evidence](#required-hil-matrix) for every rated board artifact.
4. Check hashes, results, and compatibility notes against the [promotion checklist](#promotion-checklist).
5. Publish the qualified draft, then monitor the separate
   [PyPI and Pages jobs](host-app-publishing.md). A downstream failure does not undo the GitHub Release.

## Workflow Overview

The [Release workflow](../../.github/workflows/release.yml) builds firmware and host packages with checksums.
A tag push creates the gated draft; manual dispatch creates preflight artifacts only. The
[GitHub Actions guide](../development/github-actions.md#release-and-publication) shows the parallel jobs and
dependencies.
After manual promotion, PyPI and Pages publish independently; use the [publishing runbook](host-app-publishing.md)
for setup, retries, or rollback rather than treating a tag push as publication.

## Release Preflight

Before requesting HIL, confirm:

- [ ] The change is reviewed and the working tree contains no unintended files.
- [ ] The version follows the [release tag policy](#versioning).
- [ ] The exact commit passes PR Check, host validation, required target builds, and host package checks, as specified
   in [publication gates](#publication-gates).
- [ ] `git diff --check` passes.
- [ ] Release notes identify USB/HID compatibility or behavior changes.

Do not begin physical testing from an uncommitted or locally modified image.

## Publication Gates

Normal publication is fail-closed. Draft creation waits for host validation, firmware builds, both host package jobs,
and a successful **PR Check** run for the exact commit. PyPI publication and release-mode Pages deployment additionally
require a completed, successful **Release** tag-push run for that commit and tag. The
[publication gate](../../tools/release/require_release_checks.py) checks the latest matching run and attempt; missing,
queued, running, failed, skipped, cancelled, or unavailable check results block publication. An older successful run
does not override a newer failed or pending run. Retry publication after the required checks pass.

Explicit manual test modes are the only exception to the full cross-project gate. They still run their own scoped
checks, use visibly non-release versions, and do not create or promote a qualified firmware release:

| Manual Action | Output | Version |
| --- | --- | --- |
| Publish .NET Packages | Downloadable run artifacts, not GitHub Release attachments | `VERSION-test.RUN_ID.RUN_ATTEMPT` |
| Publish Python to PyPI with `test_publish` checked | Public, immutable development release on production PyPI | `0.0.0.devRUN_ID` |
| Publish WebHID Pages with `test_publish` checked | The normal Pages site, replacing its current deployment | `0.0.0-test.RUN_ID.RUN_ATTEMPT` |

The run identifiers are supplied by GitHub. No Git tag needs to exist for these test modes, but independent .NET
packaging still requires its base **tag** version label. Environment approval and branch restrictions still apply.
Test versions are not HIL qualification or proof that the full test suite passed. Follow the
[manual publishing procedures](host-app-publishing.md) for each channel.

## Security Reporting Setup

Before publishing, enable private vulnerability reporting in repository settings and handle reports privately. User
instructions and device-security notes are in [`SECURITY.md`](../../SECURITY.md).

## USB identity

PicoUart artifacts use `cafe:4010`, an unallocated lab identity. Do not reuse it commercially; derivatives need an
allocated VID/PID and must treat the identity change as a breaking USB change. See [`SECURITY.md`](../../SECURITY.md)
and the USB identity contract tests before publishing.

## Release HIL Gates

Use the [Test Documentation Index](../tests/README.md) for automated validation, HIL sequence, and result semantics. CI
validates builds and host tests; it cannot establish physical qualification.

Release promotion requires a complete `PASS` on every rated board artifact in the release workflow. Run HIL on the exact
packaged UF2/ELF and verify its SHA-256 against the draft; a local rebuild or a result for another artifact does not
qualify the release. Optional overclock profiles are board-local configuration settings; any overclock claim still
requires exact-board HIL evidence and does not establish general stability or operating margins.

Record the active clock profile and reported system clock as well as the image hash. Persistent board configuration is
separate from the firmware artifact; matching its hash does not prove rated-clock operation. Check the boot selection
and any fallback reason before attributing results to a rated image or an overclock profile.

Follow the [HIL fixture setup](../tests/hil-fixture-setup.md) and [test plan](../tests/hil-fixture-test-plan.md) for
required phases and acceptance criteria. Preserve per-board records using the
[HIL record format](../tests/records/README.md), with artifact hashes and any failures or partial conditions.
Without matching evidence, the release remains lab-only.

## Flashing Release Artifacts

The commands below program the packaged ELF through a Debug Probe; they do not build a replacement image.
Identify the board and probe and obtain authorization before flashing or using recovery commands. Replace all example
placeholders with the selected target and artifact; do not pass a UF2 file to an `--elf` option.

```sh
tools/firmware/load.sh --board pico --skip-build --elf /path/to/release-pico.elf
```

For Pico 2, use `--board pico2` with its matching packaged ELF, not a Pico image.

Use `--probe-serial <serial>` when more than one CMSIS-DAP probe is attached or when USB enumeration tools are
unavailable.

<details>
<summary>Probe permissions and unknown flash-device troubleshooting</summary>

On Linux, `unable to find a matching CMSIS-DAP device` can mean USB access is denied even when `lsusb` lists the probe.
The loader checks the USB node permissions for a uniquely enumerated probe and retries OpenOCD with noninteractive
sudo when available, rather than resetting an inaccessible device. If sudo is not authorized, configure a
Debug Probe-specific udev rule for your user/group or authorize sudo before retrying. To distinguish permissions from
probe discovery failures without programming flash, run:

```sh
openocd -d3 -f interface/cmsis-dap.cfg -c 'transport select swd' -c 'adapter speed 1000' -c 'init; shutdown'
```

Use a current OpenOCD CMSIS-DAP build. If OpenOCD reports `Unknown flash device` after detecting the SWD target, update
OpenOCD and retry with `--adapter-speed-khz 1000` before treating the HIL attempt as firmware failure. Flash ID
`0x00154068` is a Boya BY25Q16ES device; older OpenOCD builds may need an upstream binary selected with `--openocd-exe`.

</details>

### Override QSPI Auto-Detection (Advanced)

<details>
<summary>Use only with stable SWD access and independently confirmed flash capacity</summary>

Only use this recovery when SWD communication is stable and the exact QSPI flash capacity has been confirmed from the
chip marking and its datasheet. Do not guess the capacity: OpenOCD uses it to define erase bounds, so a wrong value can
make flash operations unsafe. Prefer a current OpenOCD build and normal JEDEC/SFDP detection whenever possible.

The OpenOCD RP2040/RP2350 target scripts accept a nonzero `FLASHSIZE` in bytes to skip QSPI JEDEC/SFDP auto-detection.
Set `FLASH_SIZE_BYTES` to that confirmed capacity, then create a temporary target config before sourcing the target:

```sh
: "${FLASH_SIZE_BYTES:?Set the flash capacity in bytes from the verified chip marking and datasheet}"
OPENOCD_TARGET_CFG="$(mktemp)"
trap 'rm -f "$OPENOCD_TARGET_CFG"' EXIT
printf 'set FLASHSIZE %s\nsource [find target/rp2040.cfg]\n' "$FLASH_SIZE_BYTES" > "$OPENOCD_TARGET_CFG"
tools/firmware/load.sh --board pico --skip-build --elf /path/to/pico.elf \
   --openocd-target "$OPENOCD_TARGET_CFG" --probe-serial "<probe-serial>" --adapter-speed-khz 1000
```

For Pico 2, use `--board pico2`, a Pico 2 ELF, and replace the target in the temporary config with
`target/rp2350.cfg`. Continue only if OpenOCD reports **Verified OK**. This override skips flash identification; it does
not recover failed SWD access, repair reset/wiring, or prove the selected capacity is correct.

</details>

## Required HIL Matrix

Record coverage for each rated artifact before promotion. The [fixture plan](../tests/hil-fixture-test-plan.md) owns
traffic commands and evidence interpretation; it is not, by itself, the whole release qualification matrix.

| Coverage | Required Evidence |
| --- | --- |
| Functional fixture | Both crossed pairs in both directions and both loopbacks; all six CDC ports and HID enumerate |
| Performance | Pair and concurrent payload integrity, declared rates/durations, and clean retained HID health |
| Stability | Repeated/soak results at the declared workload; record duration and conditions rather than assuming a short sweep proves stability |
| Control and recovery | Rapid line-coding changes, disconnect/remount, watchdog recovery, and DMA wrap/re-arm checks |
| Artifact identity | Board target, firmware/runner revisions, exact artifact SHA-256, flashing evidence, and complete results |

Do not infer unrecorded cases from an aggregate `PASS`. Missing cases are `PARTIAL`; unexplained loss, timeout,
disconnect, or relevant HID error blocks qualification. In particular, an incremental ceiling search can report a
successful aggregate result despite failed boundary rows; it does not qualify those rates. The
[record index](../tests/records/README.md#evidence-index) describes historical coverage limits, not substitutes for a
current exact-artifact run.

## Optional Claims

Run optional tests only when the release notes claim the behavior:

- Backpressure and hardware/PIO RTS/CTS require the matching board configuration and HIL variants described in the
   [fixture test plan](../tests/hil-fixture-test-plan.md).

## Promotion Checklist

- [ ] All required build and check gates passed for the exact release commit.
- [ ] Every rated firmware artifact has complete, passing evidence for the required scope, with matching hashes.
- [ ] Records and necessary transcripts are accessible; local cache paths alone are not a shared evidence archive.
- [ ] Release notes disclose compatibility changes, tested conditions, and any unqualified optional claims.

Once every item is satisfied, promote the draft. Diagnose PyPI or Pages failures separately; never overwrite a
published PyPI version.

## Versioning

Every user-facing release-version input is named **tag** and accepts `vMAJOR.MINOR.PATCH`, for example `v1.2.3`.
Unprefixed `1.2.3` is rejected. Firmware and .NET use the validated resolver value; Python tag builds derive their
version through `hatch-vcs`. All three identify the same unprefixed version, `1.2.3`, and packaging checks verify that
match.

For **Release** preflight and independent **Publish .NET Packages** runs, this input is a base version label; it need
not already exist as a Git tag, does not create a tag, and does not change which branch/commit is checked out.
Stable PyPI releases and release-mode WebHID publication require an existing, published, qualified release tag.
WebHID and Python development-publish modes leave **tag** empty and generate their marked test versions automatically.

Tags use plain `vMAJOR.MINOR.PATCH` with no pre-release suffix. The release version resolver owns tag bounds; firmware
build limits may differ. Firmware reports the semantic version over HID and major/minor in `bcdDevice`; see the
[HID Report Reference](../design/usb/hid-report-reference.md) for encoding details. Release workflows own the SDK and
toolchain pins and install the reviewed, hash-locked Python requirements.
