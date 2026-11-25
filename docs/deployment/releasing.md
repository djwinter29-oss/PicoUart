# Releasing PicoUart

A release has two independent halves:

1. **Publication** is automatic. Pushing a version tag builds, tests, and publishes the firmware and host packages.
2. **Hardware qualification** is manual. Someone flashes the exact published firmware and runs the HIL fixture.

A green CI run covers software checks only. Published artifacts remain lab/test-only until hardware qualification
evidence exists.

## Release Sequence

1. Complete the [preflight](#release-preflight).
2. Push the `vMAJOR.MINOR.PATCH` tag. The Release workflow publishes the GitHub Release once its gates pass, and the
   PyPI and Pages publishers follow automatically ([host app publishing](host-app-publishing.md)).
3. [Flash the packaged images](#flashing-release-artifacts) without rebuilding, and collect the
   [required HIL evidence](#required-hil-matrix) for each rated board.
4. Confirm the [release HIL gates](#release-hil-gates) before calling the release hardware-qualified.

The CI job graph is described in [GitHub Actions](../development/github-actions.md).

## Release Preflight

Before tagging, make sure the change is reviewed, the tree has no stray files, and `git diff --check` is clean. Pick
the version using the [versioning policy](#versioning), note USB/HID compatibility changes for the release notes, and
confirm private vulnerability reporting is enabled in the repository settings ([SECURITY.md](../../SECURITY.md)).

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

For regular setup or updates, follow [Installing Firmware](../hardware-setup.md#installing-firmware). For a release
qualification run, verify the artifact's SHA-256 against the release manifest, then use the SWD procedure in
[Firmware Build and Configuration](../../firmware/build-and-config.md#debug-probe-loading-and-recovery). Do not
rebuild the image being qualified.

## Release HIL Gates

A release is hardware-qualified only when every rated board has a complete `PASS` for this matrix. The
[fixture setup](../tests/hil-fixture-setup.md) and [test plan](../tests/hil-fixture-test-plan.md) define the
procedures; keep one record per board in the [HIL records](../tests/records/README.md) format.

<a id="required-hil-matrix"></a>

| Coverage | Required evidence |
| --- | --- |
| Functional fixture | Both crossed pairs in both directions and both loopbacks; all six CDC ports and HID enumerate |
| Performance | [Default six-port sweep](../tests/hil-fixture-test-plan.md#standard-combined-run), 30 s/rate, clean HID; pair sweeps for pair-specific claims |
| Stability | Repeat or soak the intended workload; record workload and duration in the HIL record before testing |
| Control and recovery | Rapid line-coding changes, disconnect/remount, watchdog recovery, and DMA wrap/re-arm |
| Artifact identity | Board target, firmware and runner revisions, and the tested artifact's SHA-256 |

Missing cases make the result `PARTIAL`. Unexplained loss, timeouts, disconnects, or HID errors block qualification.
A speed-sweep `PASS` qualifies only the 115200-baud baseline. The boundary it finds is a measurement, and higher
rates remain unrated. Older [record index](../tests/records/README.md#evidence-index) entries do not replace a current
exact-artifact run.

The [test plan's Known Gaps](../tests/hil-fixture-test-plan.md#known-gaps) lists hardware cases without validated
procedures. A required case cannot pass until its procedure and acceptance criteria are validated. Optional claims
such as RTS/CTS or backpressure need their matching board configuration and HIL variant.

## Versioning

Release and packaging workflows accept a **tag** in `vMAJOR.MINOR.PATCH` form (for example `v1.2.3`); unprefixed
versions and pre-release suffixes are rejected. Firmware, the Python wheel, and the .NET host all report the same
unprefixed version, and packaging checks verify that they match. Firmware reports it over HID and puts major/minor in
USB `bcdDevice`.

For a manual Release preflight or .NET packaging run, **tag** is only a version label: it does not need to exist and is
not created. Stable PyPI uploads and release-mode Pages deployments require an existing published release tag.
