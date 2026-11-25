# GitHub Actions: Validation and Releases

PR Check and Release both reuse [Host Validation](../../.github/workflows/host-validation.yml); Release adds packaging
and publication gates. Workflow files define job names, matrices, toolchain pins, triggers, and dependencies.

[PR Check](../../.github/workflows/pr-check.yml) runs for pull requests and pushes to `main`. The firmware matrix and
host validation run in parallel; both must pass. Repository pytest covers hardware-free HIL-tool tests and
packaging/protocol contracts.

## Validation Gates

| Gate | What it checks |
| --- | --- |
| Firmware builds | Pico and Pico 2 compile/link, pass binary contracts, and meet target-specific static SRAM/flash budgets |
| Firmware policy | The SDK build leaves regulator auto-adjust disabled, based on compile commands, defines, and linked artifacts |
| Host applications | Python and .NET tests, plus installed/published host contracts |
| Linux host tools | Native C sanitizers, syntax checks, static analysis, and CLI smoke checks |
| Minimum Python | Repository Python checks on the minimum supported interpreter |
| Browser | JavaScript syntax, Direct Dashboard browser tests, and the shared dashboard contract test under `host/web/tests` |
| Python wheel | Version, clean installation outside the checkout, and bundled dashboard startup response |
| .NET archives | Self-contained publish, archive extraction, CLI startup, and embedded version |

CI has no board. Run [physical HIL](../tests/README.md) separately for UART payload integrity, health, timing, and
recovery behavior. Qualify the exact candidate artifacts using the
[release HIL gates](../deployment/releasing.md#release-hil-gates).

## Release and Publication

[Release](../../.github/workflows/release.yml) runs on a version-tag push or as a manual preflight; only a tag push
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

See [releasing](../deployment/releasing.md) for publication eligibility and
[host app publishing](../deployment/host-app-publishing.md) for channel operations.

When workflows change their triggers, gate dependencies, or what the checks establish, update this map too.
