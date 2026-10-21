# Host Distribution Plan

Status: staged rollout. The firmware release workflow packages firmware and runs host validation. WebHID Pages
deployment is implemented, subject to the [repository setup](../releasing.md#webhid-pages) and a successful deployment.
PyPI publication, .NET downloads/installers, and a downloadable WebHID archive remain planned, not available through
the current release workflow.

## Distribution Channels

| Application | First phase | Later phase |
| --- | --- | --- |
| Python | Wheel and source distribution on PyPI; recommend `pipx` for application installation | No native installer planned |
| .NET | Self-contained archives on GitHub Releases, with no installed .NET runtime required | Linux `.deb`, signed Windows `.msi`, signed and notarized macOS `.app` |
| WebHID | Static HTTPS site on GitHub Pages and a downloadable static archive | No application installer or backend planned |

Keep Python/.NET assets in their existing shared frontend. WebHID remains independent, with all deployable files in
`host/webhid`; do not introduce a shared asset-generation or preparation step.

## Python Publication

Publish the existing `pico-uart` host package to PyPI. Once publication is available, recommend
`pipx install pico-uart` to isolate dependencies and avoid modifying an OS-managed Python environment. Keep Linux-only
HIL tooling outside this cross-platform package.

Use PyPI Trusted Publishing with GitHub Actions OIDC rather than a stored API token. Before enabling it, confirm PyPI
project ownership/name availability and configure the trusted repository, publishing workflow, and protected GitHub
environment. Grant publication permissions only to the publishing job.

Build a wheel and source distribution, then rebuild a wheel from the source distribution. Install and smoke-test both
wheel paths in clean environments outside the checkout. Verify the CLI, bundled dashboard assets, and runtime
dependencies on Linux, Windows, and macOS. Do not rely on editable installs or source-tree asset fallbacks as release
evidence. See [host development](host-tools.md#python-distribution) for the existing package build entry point.

## .NET Downloads

Start with runtime-specific self-contained archives: `.tar.gz` on Linux/macOS and `.zip` on Windows. Select and
document supported OS/architecture combinations before publishing; cross-platform source tests alone do not establish
that every runtime identifier works. Include the shared dashboard assets and required native HID dependencies.
Self-contained does not necessarily mean a single executable or the absence of OS library requirements.

Test each packaged archive after extraction on its target OS, outside the checkout and without relying on an installed
.NET runtime. Verify CLI startup, loopback dashboard startup/shutdown, served assets, and the HTTP contract. Record
native device-access results separately from hardware-free checks. Existing manual publish instructions remain in the
[.NET host guide](../../host/dotnet/README.md#publish).

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

The [WebHID Pages workflow](../../.github/workflows/webhid-pages.yml) deploys `host/webhid` as-is over HTTPS when a
non-prerelease GitHub Release is published. Draft creation and tag pushes alone do not deploy. Manual deployment
accepts an already-published release tag for redeployment or rollback. See the
[Pages release procedure](../releasing.md#webhid-pages) for repository setup and operational steps.

An explicit [test-publish option](../releasing.md#test-publish-from-a-branch) can deploy a selected branch commit without
a release. It runs the same WebHID checks and replaces the live Pages site, rather than creating a separate preview.

A versioned static release archive for self-hosting remains planned. It will require HTTPS or localhost when served;
opening HTML directly from disk is not a supported deployment.

Use supported desktop Chromium browsers, such as Chrome and Edge; do not promise Safari, Firefox, or mobile support.
USB access stays local to the browser's computer and requires user-granted browser permission plus OS device access.
Hosting the page does not proxy remote USB devices or require a Python/.NET server. See the
[WebHID guide](../../host/webhid/README.md) for setup, permissions, and browser behavior.

Before deployment, run WebHID tests and check the deployed project-site path, asset loading, secure-context behavior,
and browser/device compatibility. Mocked tests do not prove native WebHID access.

## Release Order And Gates

1. Agree supported OS/architecture targets, host version policy, and documented firmware/HID compatibility. Ensure
   package metadata, archive names, and release notes identify the intended host version; do not silently publish the
   package's current development version for every firmware tag.
2. Add artifact build and installation checks to CI before enabling external publication. Keep Linux native C
   sanitizers and Windows native C tests alongside cross-platform Python/.NET validation and independent WebHID checks.
3. Configure PyPI Trusted Publishing and GitHub Pages permissions/environments. Build immutable artifacts once, record
   checksums, and publish those tested artifacts after approval rather than rebuilding during publication.
4. Ship PyPI packages, .NET archives, and the Pages site as the first phase. Document actual installation/download URLs
   in the application guides only when available. Test upgrades and retain earlier archives and deployment references.
5. Add native installers when user demand justifies their signing, installation, upgrade, and removal obligations.

Host artifact checks do not replace [firmware release qualification](../releasing.md#release-hil-gates). Firmware
promotion still requires exact-artifact HIL evidence. For host releases, record the firmware used for device checks,
supported HID contract, tested platforms, and any unverified compatibility claims in release notes.