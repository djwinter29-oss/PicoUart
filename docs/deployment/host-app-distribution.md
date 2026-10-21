# Host App Distribution

The release workflow packages firmware, a Python wheel, and self-contained .NET archives. Host artifacts and firmware
share the resolved release tag version. The [WebHID dashboard](https://djwinter29-oss.github.io/PicoUart/) is hosted on
GitHub Pages, and the [Python package](https://pypi.org/project/pico-uart/) is available on PyPI. Stable publications remain
subject to the [release gates](releasing.md#publication-gates); deployment and publisher setup are documented under
[WebHID Pages](releasing.md#webhid-pages) and [PyPI publication](releasing.md#pypi-publication).
Python source-distribution release assets, native installers, and a downloadable WebHID archive remain planned.

## Distribution Channels

| Application | First phase | Later phase |
| --- | --- | --- |
| Python | Release wheel on GitHub and PyPI; recommend `pipx` for application installation | Source distribution after rebuild qualification; no native installer planned |
| .NET | Self-contained archives on GitHub Releases, with no installed .NET runtime required | Linux `.deb`, signed Windows `.msi`, signed and notarized macOS `.app` |
| WebHID | Static HTTPS site on GitHub Pages | Downloadable static archive; no application installer or backend planned |

Keep Python/.NET assets in their existing shared frontend. WebHID remains independent, with all deployable files in
`host/webhid`; do not introduce a shared asset-generation or preparation step.

## Python Publication

The release workflow attaches a versioned Python wheel to the draft GitHub Release and verifies an installation
outside the checkout. Download the wheel from a published release and install it with `pipx install /path/to/file.whl`
or with `python -m pip install /path/to/file.whl` inside a virtual environment. Both package metadata and the bundled
dashboard identify the release version through installed package metadata, without runtime configuration.

The [PyPI workflow](../../.github/workflows/publish-pypi.yml) publishes the verified GitHub Release wheel without
rebuilding it. It runs after release publication, not tag creation, and supports manual retries for published tags.
See the [publication procedure](releasing.md#pypi-publication) for Trusted Publisher registration, approval, and
immutable-version behavior. Source-distribution publication is not enabled in this initial workflow.

Use `pipx install pico-uart` for stable releases, or `pipx install --pip-args="--pre" pico-uart` to allow development
releases. These isolate application dependencies without modifying an OS-managed Python environment; see the
[Python guide](../../host/python/README.md#install) for installation details. Keep Linux-only HIL tooling outside this
cross-platform package.

Before adding source-distribution publication, build a wheel and source distribution, then rebuild a wheel from the
source distribution. Install and smoke-test both
wheel paths in clean environments outside the checkout. Verify the CLI, bundled dashboard assets, and runtime
dependencies on Linux, Windows, and macOS. Do not rely on editable installs or source-tree asset fallbacks as release
evidence. See [host development](../development/host-tools.md#python-distribution) for the existing package build entry point.

## .NET Downloads

The release workflow attaches runtime-specific self-contained archives: `.tar.gz` on Linux/macOS and `.zip` on Windows,
initially targeting Linux x64, Windows x64, and macOS arm64. Other architectures are not supplied by this matrix.
Archives include the shared dashboard; the application passes its assembly's tag version to that dashboard, with no
installed .NET runtime required.
Self-contained does not mean a single executable or the absence of OS library/device-permission requirements.
The [.NET packaging workflow](../../.github/workflows/publish-dotnet.yml) owns archive names, the target matrix, and
per-target checksums. The [release workflow](../../.github/workflows/release.yml) calls it after host validation and
attaches its artifacts alongside the separately built Python package and firmware. The .NET workflow does not build
Python packages or publish independently to an existing GitHub Release.

The [.NET packaging procedure](releasing.md#net-packaging) owns release invocation, manual test-run inputs,
artifact retrieval, and version-marker behavior. Independent manual runs produce marked test artifacts, not public
release attachments; normal release packaging remains subject to the shared publication gates.

Each host packaging job extracts its archive and checks native CLI startup and the bundled version before checksumming.
The release draft waits for all host packages, host validation, and firmware builds. Publishing the qualified draft
makes the host downloads available and triggers the separate WebHID deployment with the same tag version. Creating a
tag alone does not publish the draft or replace the hosted site.

Test each packaged archive after extraction on its target OS, outside the checkout and without relying on an installed
.NET runtime. Verify CLI startup, loopback dashboard startup/shutdown, served assets, and the HTTP contract. Record
native device-access results separately from hardware-free checks. Existing manual publish instructions remain in the
[.NET host guide](../../host/dotnet/README.md#develop-and-publish).

Add installers only after archive deployment and lifecycle behavior are stable:

- Linux: a `.deb` with explicit supported distributions, dependencies, upgrades, removal, and least-privilege HID access
  setup. Do not grant broad USB permissions or silently elevate privileges.
- Windows: a signed `.msi` with predictable installation, upgrades, uninstall, and user-invoked launch behavior.
- macOS: a signed and notarized `.app` with an ergonomic launcher before considering a `.pkg` for managed deployment.
  Define server startup, browser opening, shutdown, and architecture support; an installer alone does not solve these.

Archives must retain the current loopback-only server behavior. Neither archives nor installers should install an
always-running service by default. Signing identities, notarization access, and installer tooling are prerequisites
for the later phase, not part of the initial archive rollout.

## WebHID Hosting

The [WebHID Pages workflow](../../.github/workflows/publish-webhid.yml) deploys `host/webhid` as-is over HTTPS when a
non-prerelease GitHub Release is published. Draft creation and tag pushes alone do not deploy. Manual deployment
accepts an already-published release tag for redeployment or rollback. See the
[Pages release procedure](releasing.md#webhid-pages) for repository setup and operational steps.

An explicit [test-publish option](releasing.md#test-publish-from-a-branch) can deploy a selected branch commit without
a release or completed cross-project checks. It runs the WebHID checks, displays a test-version marker, and replaces
the live Pages site, rather than creating a separate preview.

A versioned static release archive for self-hosting remains planned. It will require HTTPS or localhost when served;
opening HTML directly from disk is not a supported deployment.

Use supported desktop Chromium browsers, such as Chrome and Edge; do not promise Safari, Firefox, or mobile support.
USB access stays local to the browser's computer and requires user-granted browser permission plus OS device access.
Hosting the page does not proxy remote USB devices or require a Python/.NET server. See the
[WebHID guide](../../host/webhid/README.md) for setup, permissions, and browser behavior.

Before deployment, run WebHID tests and check the deployed project-site path, asset loading, secure-context behavior,
and browser/device compatibility. Mocked tests do not prove native WebHID access.

## Release Order And Gates

The [release workflow overview](releasing.md#workflow-overview) owns entry points, dependencies, draft promotion,
and downstream publication order. The [publication gates](releasing.md#publication-gates) define required check
results and explicit manual test exceptions. Publisher registration, environment setup, retry, and rollback procedures
also live in that guide; this document owns distribution choices, supported downloads, and deferred installer work.

Host artifact checks do not replace [firmware release qualification](releasing.md#release-hil-gates). Firmware
promotion still requires exact-artifact HIL evidence. For host releases, record the firmware used for device checks,
supported HID contract, tested platforms, and any unverified compatibility claims in release notes.