# GitHub Actions: Validation and Releases

This guide maps the current automated checks and release dependencies. The linked workflows are authoritative for job
names, matrices, and toolchain pins. Use the [test index](../tests/README.md) for local commands and test levels,
and [Releasing PicoUart](../deployment/releasing.md) for promotion and publishing procedures.

## Pull Requests And Main

[PR Check](../../.github/workflows/pr-check.yml) runs for pull requests and pushes to `main`. Its firmware matrix and
reusable host validation run in parallel; firmware builds do not wait for host tests.

```mermaid
flowchart TD
  Event["Pull request or push to main"] --> Firmware["Firmware matrix: pico and pico2"]
  Event --> Host["Reusable Host Validation"]
  Firmware --> Contracts["Build and binary contracts<br/>Real-SDK voltage-policy check<br/>Pico-only stamped-version smoke check"]
  Host --> HostDone["All shared host checks pass"]
  Contracts --> Passed["PR Check succeeds"]
  HostDone --> Passed
```

Within a job, steps run in order. The four Host Validation branches are independent jobs, and each OS/board matrix leg
runs independently. Success boxes summarize completion; they are not additional workflow jobs. Any required failure
prevents that workflow from succeeding. Repository branch-protection settings separately control merge requirements.

Repository pytest includes hardware-free HIL-tool tests and packaging/protocol contracts, not physical UART tests.
SDK-dependent .NET integration checks can skip when no SDK executable is available; the three-OS application matrix
explicitly installs .NET and also runs its unit suite.

## Checks And Dependencies

[Host Validation](../../.github/workflows/host-validation.yml) is reused by both PR Check and Release.

```mermaid
flowchart TD
  Host["Host Validation"] --> Apps["Python + .NET<br/>Linux / macOS / Windows"]
  Host --> Native["Linux native C sanitizers<br/>Syntax, static analysis, CLI smoke"]
  Host --> Minimum["Python 3.10 minimum<br/>Repository pytest"]
  Host --> Browser["Node 22<br/>WebHID and renderer checks"]
  Apps --> Done["All shared checks pass"]
  Native --> Done
  Minimum --> Done
  Browser --> Done
```

The application-matrix job runs .NET unit tests before repository pytest; Windows runs native C tests afterward.
The Pico-only stamped-version check is a conditional step inside the PR firmware matrix, not another job.

| Check | Environment And Dependencies | What It Establishes |
| --- | --- | --- |
| Firmware matrix | Linux; Arm toolchain, pinned Pico SDK, CMake/Ninja, host C++ compiler, Python | Both board targets compile/link and satisfy binary USB/version contracts; real-build regulator policy is inspected, not physically measured |
| Firmware policy check | Python and `requirements-voltage-lock.txt` | Minimal pytest environment for inspection of the actual SDK build artifacts |
| Host application matrix | Linux, macOS, Windows; Python 3.12, .NET 10 SDK, `requirements-lock.txt` | .NET unit tests, repository Python tests, and installed/published host contracts; Windows also runs native C tests |
| Linux host tools | Python 3.12, development lock, native C toolchain, CMake/Ninja, cppcheck | Syntax, ASan/UBSan native tests, static analysis, and CLI smoke checks |
| Python minimum | Linux; Python 3.10 and development lock | Repository Python checks on the minimum supported interpreter |
| Browser host | Linux; Node 22 | JavaScript syntax and hardware-free WebHID/shared-renderer contracts |
| Release Python wheel | Python build backend/plugin and `requirements-runtime-lock.txt` | Wheel version, clean installation outside the checkout, and bundled dashboard startup response |
| Release .NET archives | Native Linux/Windows/macOS runners and .NET SDK | Self-contained publishing for the configured runtime matrix, archive extraction, CLI startup, and embedded version |
| Physical HIL | Identified board, intended flashed artifact, and required fixture | UART payload integrity, health, timing/recovery behavior, and conditions absent from CI |

Locks contain reviewed versions and hashes; the
[host development guide](host-tools.md#python-dependencies-and-locking)
owns their declarations and generation. Version stamping and host transport checks do not establish board compatibility
or physical overclock/voltage margins. See [HIL scope](../tests/hil-fixture-test-plan.md) for the evidence limits.

## Release And Publication

[Release](../../.github/workflows/release.yml) runs on a version-tag push or manual preflight. Firmware packaging starts
alongside Host Validation. Python and .NET packaging wait for **all** Host Validation jobs, not for firmware packaging.
Only a tag push enters the draft-publication job.

```mermaid
flowchart TD
  Tag["Version-tag push"] --> Host["Reusable Host Validation"]
  Tag --> Firmware["pico / pico2<br/>Build, verify, package, checksum"]
  Host --> Python["Python wheel<br/>Build, install-check, checksum"]
  Host --> Dotnet[".NET native matrix<br/>Publish, extract-check, checksum"]
  Host --> Gate["Draft-publication job"]
  Firmware --> Gate
  Python --> Gate
  Dotnet --> Gate
  PR["Successful PR Check<br/>for the exact release commit"] -.->|"Explicit gate check"| Gate
  Gate --> Draft["Draft GitHub Release<br/>Packaged artifacts and checksums"]
  Draft --> HIL["Manual HIL on exact artifacts<br/>Every rated board and required case"]
  HIL --> Qualified{"Complete PASS<br/>and matching hashes?"}
  Qualified -->|"No"| Hold["Keep draft; diagnose or rerun"]
  Qualified -->|"Yes"| Promote["Manually publish qualified draft"]
  Promote --> PyPI["PyPI: release/checksum/version gates<br/>Protected upload of unchanged wheel"]
  Promote --> Pages["Pages: release/check gates<br/>Validated commit, Node checks, version stamp"]
```

The exact-commit PR Check is a **separate workflow requirement**, not a Release `needs` job. A tag push does not itself
trigger PR Check. A successful PR run for a different merge commit does not satisfy this gate; confirm a successful
matching run, commonly from the push to `main`, before releasing that commit.

CI creates the draft but does not perform physical HIL or automatically qualify/promote it. Artifact hashes identify
the tested image; record the board's active clock profile and boot fallback too. A local rebuild, a partial run, or a
ceiling-search aggregate `PASS` with failed rows cannot replace required exact-artifact evidence.

After publication, PyPI and Pages independently recheck the successful exact-commit PR Check and matching Release
tag-push run. PyPI reuses the released wheel; Pages runs its own Node checks before deployment. Repository environment
approvals still apply. Publishing via another workflow's default `GITHUB_TOKEN` does not trigger these release-event
workflows; use the documented manual release-mode retry instead.

## Manual Modes And Failures

| Mode Or Failure | Consequence |
| --- | --- |
| Manual Release preflight | Builds firmware and host artifacts with the entered version label, but creates no draft or public upload |
| Independent .NET packaging test | Native archive checks produce marked workflow artifacts; no release attachment or publication |
| Explicit Python test publication | Scoped package tests and installed-wheel checks, then an approved public development upload to production PyPI |
| Explicit Pages test publication | Scoped Node checks, then a marked deployment replacing the live site; not a private preview |
| Required automated check fails | No successful normal gate; fix or rerun before continuing |
| HIL fails, is partial, or lacks matching artifacts | Keep the release unqualified and do not promote the draft |
| PyPI or Pages fails after GitHub publication | Diagnose/retry that channel; GitHub publication and the other channel are not automatically rolled back |

Manual test modes are intentional exceptions, not equivalent substitutes for the normal release graph. The
[publication gates](../deployment/releasing.md#publication-gates) own fail-closed run selection and test-version rules;
the [publishing runbook](../deployment/host-app-publishing.md) owns setup, retries, and rollback. Changing a workflow's
triggers, `needs`, tests, or dependency setup should include reviewing this map.