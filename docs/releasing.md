# Releasing PicoUart

Release tags run [`.github/workflows/release.yml`](../.github/workflows/release.yml), which packages firmware, a Python
wheel, and standalone .NET archives, runs host validation, and opens a **draft** GitHub Release. The workflow and
[firmware build guide](../firmware/build-and-config.md) define the current board matrix, clocks, and artifact names.
The promote HIL gate covers the rated images.

Do not publish the draft until exact-artifact hardware-in-the-loop (HIL) evidence passes the gates below.

A `workflow_dispatch` run creates artifacts for preflight HIL; a release tag creates the draft to promote.

Host application publication follows the [host distribution plan](development/host-distribution.md).
[WebHID Pages](#webhid-pages) deploys separately after release publication. The Python wheel and .NET archives embed
the same tag version in their dashboards and have per-target checksums. [PyPI publication](#pypi-publication) reuses the
released wheel after its one-time setup; native installers remain planned.

The release orchestrator calls the separate [.NET packaging workflow](../.github/workflows/publish-dotnet.yml) and
waits for all targets before creating the draft. Python is packaged independently with `SHA256SUMS-python.txt`;
.NET archives retain their per-target checksum manifests. See the
[.NET distribution procedure](development/host-distribution.md#net-downloads) for packaging-only manual runs.

```mermaid
flowchart LR
   Change["Reviewed change"] --> CI["CI: host tests + rated builds"]
   CI --> DryRun["workflow_dispatch\ndraft artifacts"]
   DryRun --> Hash["Record artifact SHA-256"]
   Hash --> HIL["HIL on exact packaged images"]
   HIL --> Results["Record commands, hashes, HID health, results"]
   Results --> Compare{"Artifact hashes match?"}
   Compare -->|no| Stop["Keep lab-only\nfix or rerun"]
   Compare -->|yes| Promote["Promote draft release"]
```

The release unit is the exact packaged artifact, not a source revision or a local rebuild. HIL evidence is valid only
when its image hash matches the artifact attached to the draft release.

## Release Preflight

Before requesting HIL, confirm:

1. The change is reviewed and the working tree contains no unintended files.
2. The version follows the [release tag policy](#versioning).
3. Host validation and all required target builds pass in the release workflow.
4. `git diff --check` passes.
5. Release notes identify USB/HID compatibility or behavior changes.

Do not begin physical testing from an uncommitted or locally modified image.

## Publication Gates

Normal publication is fail-closed. Draft creation waits for host validation, firmware builds, both host package jobs,
and a successful **PR Check** run for the exact commit. PyPI publication and release-mode Pages deployment additionally
require a completed, successful **Release** tag-push run for that commit and tag. The
[publication gate](../tools/release/require_release_checks.py) checks the latest matching run and attempt; missing,
queued, running, failed, skipped, cancelled, or unavailable check results block publication. An older successful run
does not override a newer failed or pending run. Retry publication after the required checks pass.

Explicit manual test modes are the only exception to the full cross-project gate. They still run their own scoped
checks, use visibly non-release versions, and do not create or promote a qualified firmware release:

| Manual Action | Output | Version |
| --- | --- | --- |
| Publish .NET Packages | Downloadable run artifacts, not GitHub Release attachments | `VERSION-test.RUN_ID.RUN_ATTEMPT` |
| Publish Python to PyPI with `test_publish` checked | TestPyPI only, never production PyPI | `0.0.0.devRUN_ID` |
| Publish WebHID Pages with `test_publish` checked | The normal Pages site, replacing its current deployment | `0.0.0-test.RUN_ID.RUN_ATTEMPT` |

The run identifiers are supplied by GitHub; no release tag is needed for these test modes. Environment approval and
branch restrictions still apply. Test versions are not HIL qualification or proof that the full test suite passed.

## Security Reporting Setup

Before publishing, enable private vulnerability reporting in repository settings and handle reports privately. User
instructions and device-security notes are in [`SECURITY.md`](../SECURITY.md).

## USB identity

PicoUart artifacts use `cafe:4010`, an unallocated lab identity. Do not reuse it commercially; derivatives need an
allocated VID/PID and must treat the identity change as a breaking USB change. See [`SECURITY.md`](../SECURITY.md) and
the USB identity contract tests before publishing.

## Release HIL Gates

Use the [Test Documentation Index](tests/README.md) for automated validation, HIL sequence, and result semantics. CI
validates builds and host tests; it cannot establish physical qualification.

Release promotion requires a complete `PASS` on every rated board artifact in the release workflow. Run HIL on the exact
packaged UF2/ELF and verify its SHA-256 against the draft; a local rebuild or a result for another artifact does not
qualify the release. Optional overclock profiles are board-local configuration settings; any overclock claim still
requires exact-board HIL evidence and does not establish general stability or operating margins.

Follow the [HIL fixture setup](tests/hil-fixture-setup.md) and [test plan](tests/hil-fixture-test-plan.md) for required
phases and acceptance criteria. Preserve per-board records using the [HIL record format](tests/records/README.md), with
artifact hashes and any failures or partial conditions. Without matching recorded evidence, the release remains lab-only.

## Flashing Release Artifacts

Flash with the packaged ELF/UF2 and skip rebuilding:

```sh
tools/firmware/load.sh --board <pico|pico2> --skip-build --elf <path-to-release.elf>
```

Use `--probe-serial <serial>` when more than one CMSIS-DAP probe is attached or when USB enumeration tools are
unavailable.

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

### Override QSPI Auto-Detection (Advanced)

Only use this recovery when SWD communication is stable and the exact QSPI flash capacity has been confirmed from the
chip marking and its datasheet. Do not guess the capacity: OpenOCD uses it to define erase bounds, so a wrong value can
make flash operations unsafe. Prefer a current OpenOCD build and normal JEDEC/SFDP detection whenever possible.

The OpenOCD RP2040/RP2350 target scripts accept a nonzero `FLASHSIZE` in bytes to skip QSPI JEDEC/SFDP auto-detection.
Create a temporary target config that sets the verified capacity before sourcing the board target:

```sh
FLASH_SIZE_BYTES=<confirmed-capacity-in-bytes>
OPENOCD_TARGET_CFG="$(mktemp)"
trap 'rm -f "$OPENOCD_TARGET_CFG"' EXIT
printf 'set FLASHSIZE %s\nsource [find target/rp2040.cfg]\n' "$FLASH_SIZE_BYTES" > "$OPENOCD_TARGET_CFG"
tools/firmware/load.sh --board pico --skip-build --elf <path-to-pico.elf> \
   --openocd-target "$OPENOCD_TARGET_CFG" --probe-serial <probe-serial> --adapter-speed-khz 1000
```

For Pico 2, use `--board pico2`, a Pico 2 ELF, and replace the target in the temporary config with
`target/rp2350.cfg`. Continue only if OpenOCD reports **Verified OK**. This override skips flash identification; it does
not recover failed SWD access, repair reset/wiring, or prove the selected capacity is correct.

## Required HIL Matrix

Follow the [board-testing skill](../.github/skills/pico-uart-board-testing/SKILL.md), [fixture setup](tests/hil-fixture-setup.md),
and [HIL test plan](tests/hil-fixture-test-plan.md) for the current required test matrix. Promotion requires no
unexplained data loss, timeout, USB disconnect, or relevant HID error.

## Optional Claims

Run optional tests only when the release notes claim the behavior:

- Backpressure and hardware/PIO RTS/CTS require the matching board configuration and HIL variants described in the
   [fixture test plan](tests/hil-fixture-test-plan.md).

## Promote checklist (draft → published)

Before publishing, verify the draft hashes match complete HIL records for all rated targets, link those records, and
include any USB identity or HID compatibility notes in the release notes. Release CI must pass its reviewed dependency
lock and build gates.

## Versioning

Tags use plain `vMAJOR.MINOR.PATCH` with no pre-release suffix. The release version resolver owns tag bounds; firmware
build limits may differ. Firmware reports the semantic version over HID and major/minor in `bcdDevice`; see the
[HID Report Reference](design/usb/hid-report-reference.md) for encoding details. Release workflows own the SDK and
toolchain pins and install the reviewed, hash-locked Python requirements.

## PyPI Publication

The [PyPI workflow](../.github/workflows/publish-pypi.yml) runs when a non-prerelease GitHub Release is published. It
downloads the existing release wheel, checks its SHA-256 against `SHA256SUMS-python.txt` from the release, validates
the package name and tag version, and passes only that verified wheel to the protected publishing job. It does not
rebuild packages or publish on tag push, draft creation, or release preflight runs. Source-distribution publication
remains deferred until its rebuild and installation checks are implemented.

### Trusted Publisher Setup

1. Merge the workflow into the default branch so release events and manual dispatch are available.
2. Create a GitHub environment named **pypi** under **Settings > Environments**, with required reviewers. Restrict its
   deployment branches/tags to the release tags and approved branch used for manual retries.
3. Sign in to PyPI with a verified account and two-factor authentication. For an existing project you own, open its
   **Publishing** settings. For the first release, use **Your account > Publishing > Add a new pending publisher**.
   Confirm that the package name is available to your account; a missing public project does not guarantee availability.
4. Select GitHub and register these exact values:

| PyPI Field | Value |
| --- | --- |
| Project name (pending publisher) | `pico-uart` |
| Owner | `djwinter29-oss` |
| Repository | `PicoUart` |
| Workflow filename | `publish-pypi.yml` |
| Environment | `pypi` |

The filename is not the workflow display name or its full path. No PyPI API token or GitHub secret is required.
If a Trusted Publisher was registered before the workflow rename, update or replace its registration with
`publish-pypi.yml` on both PyPI and TestPyPI before the next upload. The environment names remain `pypi` and `testpypi`.
Only the publishing job receives `id-token: write`; verification and downloads run without publication credentials.
The official PyPA action verifies distribution metadata and creates publication attestations using Trusted Publishing.

### Publish And Retry

Create a normal release tag and let the release workflow attach the wheel and checksum manifest to its draft. Complete
the existing HIL promotion gates, publish the draft, and approve the **pypi** environment deployment. Verify the
result at <https://pypi.org/project/pico-uart/> before advertising the package as available.

To retry a failed publication or publish an older qualified release, run **Actions > Publish Python to PyPI > Run
workflow** with its already-published **tag**, leaving **test_publish** unchecked. The release must include the expected
wheel and checksum manifest and pass the [publication gates](#publication-gates).
Drafts, prereleases, checksum mismatches, and package/version mismatches fail before uploading. Publication initiated
by another workflow's default `GITHUB_TOKEN` does not trigger the release-event workflow; use manual dispatch then.

PyPI versions and uploaded filenames are immutable. This workflow intentionally does not skip existing distributions:
an already-published version fails visibly rather than masking a mismatch. If upload succeeded but a later step
failed, inspect PyPI before retrying; publish corrections under a new version. A PyPI failure does not roll back the
GitHub Release or Pages deployment. Production publication never falls back to TestPyPI.

### Manual TestPyPI Publication

Create a separate GitHub environment named **testpypi** and register a Trusted Publisher on
<https://test.pypi.org/>. Use the same owner, repository, workflow filename, and package name as above, but set the
publisher environment to **testpypi**. TestPyPI accounts, project ownership, and publisher registration are separate
from production PyPI; no production credentials are used for this path.

Run **Actions > Publish Python to PyPI > Run workflow**, select the branch, check **test_publish**, and leave **tag**
empty. The workflow runs Python package tests, builds `0.0.0.devRUN_ID`, checks the installed wheel outside the source
tree, and publishes it only to TestPyPI after environment approval. The full PR/release/HIL gates are not required.
The wheel is also available as the run's `testpypi-distribution` artifact.

Each new workflow run gets a different development version. Rerunning the same run keeps its version; TestPyPI rejects
an already-uploaded filename, so start a new manual run when a new test upload is needed. TestPyPI is not a production
distribution channel, and test uploads do not change the normal `pico-uart` version on PyPI.

## WebHID Pages

The [WebHID Pages workflow](../.github/workflows/publish-webhid.yml) publishes the standalone browser app from
`host/webhid`, without modifying or deploying the Python/.NET frontend. It runs when a non-prerelease GitHub Release
is published, after the firmware draft has passed its promotion gates. Pushing a tag, creating a draft, and release
preflight runs do not change the live site.

### Repository Setup

1. Merge the Pages workflow into the repository's default branch so GitHub can receive release events.
2. In **Settings > Pages > Build and deployment**, set **Source** to **GitHub Actions**. No deployment branch, custom
   domain, or personal access token is required.
3. Review the **github-pages** environment under **Settings > Environments**. Permit the release tags used for
   deployment and configure required reviewers if desired. Manual redeployment must also be allowed from the branch
   containing the workflow. Only the deployment job receives `pages: write` and `id-token: write` permissions.

After the first successful deployment, the default project URL is
<https://djwinter29-oss.github.io/PicoUart/>. The deployment job's environment URL is authoritative if Pages settings
or the repository name change. This setup does not claim the site is already live.

### Publish Or Roll Back

Publish the qualified firmware draft through GitHub's release UI to trigger deployment. The Pages workflow validates
the release tag and published/non-prerelease status, checks out that tag, runs the WebHID tests, and uploads only
`host/webhid`. Deployment runs are serialized and retain the selected tag in their workflow inputs/event record.
Before upload, the workflow stamps the visible host version from the validated release tag, without the leading `v`.
Local WebHID displays `0.0.0-debug`; explicit test publishes show `0.0.0-test.RUN_ID.RUN_ATTEMPT`. Both labels are
separate from the connected board's firmware version.
Publishing through another workflow's default `GITHUB_TOKEN` does not trigger a new release-event workflow; use the
manual deployment below in that case.

For redeployment or rollback, run **Actions > Publish WebHID Pages > Run workflow** with the desired already-published release
tag and leave **test_publish** unchecked. Drafts, prereleases, and tags outside the normal release version policy are
rejected. The selected tag must contain the standalone WebHID app and its tests. Rolling back the site does not modify
firmware releases or boards.

### Test Publish From A Branch

To publish current WebHID without creating a firmware release:

1. Commit and push the WebHID and workflow changes. The workflow must exist on the default branch for manual dispatch
   to be available; the branch selected for the run must include the test-publish option.
2. Open **Actions > Publish WebHID Pages > Run workflow** and select the desired branch under **Use workflow from**.
3. Check **test_publish**, leave **tag** empty, and run the workflow. Ensure the `github-pages` environment permits
   deployment from that branch and approve the deployment if required.
4. Open the deployment's environment URL after the run succeeds.

Test publication deploys the selected branch's exact commit, runs the WebHID checks, and skips the published-release
lookup and full cross-project gate. It replaces the site at the normal Pages URL; it is not an isolated preview.
The visible **App v** label includes the test run and attempt. Local uncommitted changes are not
included. Restore a release by rerunning with **test_publish** unchecked and the desired published **tag**.

A Pages failure does not unpublish the GitHub Release. Fix or rerun the deployment separately, and inspect the Pages
environment for the currently successful deployment. After deployment, check asset loading at the project URL and
native device access in a supported browser; automated tests do not prove physical USB compatibility.
