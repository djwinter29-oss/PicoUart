# Host App Distribution

This guide owns distribution channels, artifact boundaries, and deferred installer work. Use
[Releasing PicoUart](releasing.md) for qualification and shared gates, or [Host App Publishing](host-app-publishing.md)
for publisher setup, manual tests, retries, and rollback. Published host artifacts share the firmware release version.

## Distribution Channels

| Application | Current Channel And Workflow Output | Planned, Not Currently Published |
| --- | --- | --- |
| Python | Release wheel on GitHub and PyPI; recommend `pipx` for application installation | Source distribution after rebuild qualification; no native installer planned |
| .NET | Self-contained archives on GitHub Releases, with no installed .NET runtime required | Linux `.deb`, signed Windows `.msi`, signed and notarized macOS `.app` |
| WebHID | Static HTTPS site on GitHub Pages | Downloadable static archive; no application installer or backend planned |

Keep Python/.NET assets in their existing shared frontend. WebHID remains independent, with all deployable files in
`host/webhid`; do not introduce a shared asset-generation or preparation step.

## Python Publication

The [Python package](https://pypi.org/project/pico-uart/) is available on PyPI. Stable publication reuses the verified
GitHub Release wheel; explicit development publication uploads a marked version to the same public index.
Prefer pipx for isolated application use, or a virtual environment for Python API use. Installation and pre-release
selection belong in the [Python guide](../../host/python/README.md#install); Linux-only HIL tooling is separate.

The wheel includes the shared dashboard, with its installed package version exposed to the UI. Source archives have
standalone rebuild regression coverage but are not current release assets. Publishing them still requires explicit
workflow support and cross-platform installed-artifact qualification, not just an editable-install test.
See [Python publication](host-app-publishing.md#pypi-publication) for operational steps and
[host development](../development/host-tools.md#python-distribution) for local builds.

## .NET Downloads

The release workflow attaches runtime-specific self-contained archives: `.tar.gz` on Linux/macOS and `.zip` on Windows,
initially targeting Linux x64, Windows x64, and macOS arm64. Other architectures are not supplied by this matrix.
Archives include the shared dashboard; the application passes its assembly's tag version to that dashboard, with no
installed .NET runtime required.
Self-contained does not mean a single executable or the absence of OS library/device-permission requirements.
The [.NET packaging workflow](../../.github/workflows/publish-dotnet.yml) owns archive names, targets, and checksums.
Its extracted-artifact checks establish native CLI startup and version metadata, not physical HID/UART compatibility.
Check the actual release assets: a workflow target does not mean every historical release supplied that archive.

The [.NET host guide](../../host/dotnet/README.md#install) owns user installation. The
[packaging runbook](host-app-publishing.md#net-packaging) owns manual tests and artifact retrieval. Archive
qualification must cover the extracted app, dashboard assets, startup/shutdown, and applicable device access on its
target OS, outside the checkout and without relying on an installed .NET runtime.

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

The [hosted dashboard](https://djwinter29-oss.github.io/PicoUart/) uses the standalone `host/webhid` assets over HTTPS.
Packaging stamps its visible version without introducing shared frontend generation. Normal release deployment and
explicit live-site test deployment are separate modes; see the [Pages runbook](host-app-publishing.md#webhid-pages).

A versioned static release archive for self-hosting remains planned. It will require HTTPS or localhost when served;
opening HTML directly from disk is not a supported deployment.

Use supported desktop Chromium browsers, such as Chrome and Edge; do not promise Safari, Firefox, or mobile support.
USB access stays local to the browser's computer and requires user-granted browser permission plus OS device access.
Hosting the page does not proxy remote USB devices or require a Python/.NET server. See the
[WebHID guide](../../host/webhid/README.md) for setup, permissions, and browser behavior.

Qualification includes the deployed project-site path, asset loading, secure-context behavior, and supported
browser/device access. Mocked checks do not prove native WebHID compatibility.

## Release Order And Gates

The [release guide](releasing.md#workflow-overview) owns ordering, draft promotion, and shared gates. The
[publishing runbook](host-app-publishing.md) owns channel-specific operations. Host checks do not replace firmware
HIL: record tested firmware, supported HID contract, platforms, and unverified compatibility claims in release notes.