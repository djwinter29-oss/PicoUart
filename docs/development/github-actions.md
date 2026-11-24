# GitHub Actions: Validation and Releases

PR Check and Release share host validation; Release adds packaging and publication gates. The workflow files are the
source of truth for job names, matrices, and toolchain pins.

## Pull Requests and Main

[PR Check](../../.github/workflows/pr-check.yml) runs for pull requests and pushes to `main`.

```mermaid
flowchart TD
  Event["Pull request or push to main"] --> Firmware["Firmware matrix: pico and pico2"]
  Event --> Host["Reusable Host Validation"]
  Firmware --> PRCheck["PR Check"]
  Host --> PRCheck
```

The firmware matrix and host validation run in parallel; both must pass for PR Check to succeed. Repository pytest
covers hardware-free HIL-tool tests and packaging/protocol contracts, not physical UART behavior.

## Checks and Dependencies

[Host Validation](../../.github/workflows/host-validation.yml) is reused by both PR Check and Release.

```mermaid
flowchart TD
  Host["Host Validation"] --> Apps["Python + .NET<br/>Linux / macOS / Windows"]
  Host --> Native["Linux native C sanitizers<br/>Syntax, static analysis, CLI smoke"]
  Host --> Minimum["Python 3.10 minimum<br/>Repository pytest"]
  Host --> Browser["Node 22<br/>WebHID and renderer checks"]
```

| Check | Environment and dependencies | What it checks |
| --- | --- | --- |
| Firmware matrix | Linux; Arm toolchain, pinned Pico SDK, CMake/Ninja, host C++ compiler, Python | Pico and Pico 2 compile/link, pass binary contracts, and satisfy target-specific static SRAM/flash budgets |
| Firmware policy check | Python and the `test` dependency group | Real SDK build leaves regulator auto-adjust disabled, based on compile commands, defines, and linked artifacts |
| Host application matrix | Linux, macOS, Windows; Python 3.12, .NET 10 SDK, Python `dev` dependency group | .NET unit tests, repository Python tests, and installed/published host contracts; Windows and macOS also run native C tests |
| Linux host tools | Python 3.12, Python `dev` dependency group, native C toolchain, CMake/Ninja, cppcheck | Syntax, ASan/UBSan native tests, static analysis, and CLI smoke checks |
| Python minimum | Linux; Python 3.10 and Python `dev` dependency group | Repository Python checks on the minimum supported interpreter |
| Browser host | Linux; Node 22 | JavaScript syntax, WebHID tests, and the shared dashboard contract test under `host/web/tests` |
| Release Python wheel | Python build backend/plugin and wheel-declared runtime dependencies | Wheel version, clean installation outside the checkout, and bundled dashboard startup response |
| Release .NET archives | Native Linux/Windows/macOS runners and .NET SDK | Self-contained publishing for the configured runtime matrix, archive extraction, CLI startup, and embedded version |

CI has no board. Run [physical HIL](../tests/README.md) separately for UART payload integrity, health, timing, and
recovery behavior.

## Release and Publication

[Release](../../.github/workflows/release.yml) runs on a version-tag push or as a manual preflight. Firmware packaging
starts alongside host validation; the Python and .NET packages wait for all host validation jobs. Only a tag push
publishes.

```mermaid
flowchart TD
  Tag["Version-tag push"] --> Host["Reusable Host Validation"]
  Tag --> Firmware["pico / pico2<br/>Build, verify, package, checksum"]
  Host --> Python["Python wheel<br/>Build, install-check, checksum"]
  Host --> Dotnet[".NET native matrix<br/>Publish, extract-check, checksum"]
  Host --> Gate["GitHub-publication job"]
  Firmware --> Gate
  Python --> Gate
  Dotnet --> Gate
  PR["Successful PR Check<br/>for the exact release commit"] -.->|"Explicit gate check"| Gate
  Gate --> Release["Published GitHub Release<br/>Packaged artifacts and checksums"]
  Release --> Complete["Successful Release workflow completion"]
  Complete --> PyPI["PyPI: release/checksum/version gates<br/>Protected upload of unchanged wheel"]
  Complete --> Pages["Pages: release/check gates<br/>Validated commit, Node checks, version stamp"]
```

PyPI and Pages run from `workflow_run` after Release completes. A release event created with `GITHUB_TOKEN` does not
start other workflows. See [releasing](../deployment/releasing.md) for publication gates and hardware qualification,
and [host app publishing](../deployment/host-app-publishing.md) for channel operations.

When you change a workflow's triggers, `needs`, tests, or dependency setup, update this map too.

## Manual Hardware Qualification

CI publishes candidate artifacts. Qualify those exact artifacts manually using the
[release HIL gates](../deployment/releasing.md#release-hil-gates).
