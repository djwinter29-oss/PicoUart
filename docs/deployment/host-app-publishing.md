# Host App Distribution and Publishing

The host applications share the firmware release version. A successful tag-push Release run publishes them
automatically ([releasing](releasing.md)).

## Distribution Channels

| Application | Published channel |
| --- | --- |
| Python | Wheel on GitHub Releases and [PyPI](https://pypi.org/project/pico-uart/) |
| .NET | Self-contained archives on GitHub Releases (Linux x64, Windows x64, macOS arm64) |
| Direct Dashboard | Static site on [GitHub Pages](https://djwinter29-oss.github.io/PicoUart/) |

The Python and .NET packages bundle the shared dashboard from `host/web/src`. The Direct Dashboard is independent: it
shares no assets with them and deploys `host/direct/src` as-is.

## Manual Test Modes

Each channel has a manual test mode for trying changes from a branch. Test modes skip the full release gate, but they
still run their own checks and environment approvals. They never qualify firmware.

| Workflow | Result | Version |
| --- | --- | --- |
| Publish .NET Packages | Downloadable workflow artifacts, not release attachments | `VERSION-test.RUN_ID.RUN_ATTEMPT` |
| Publish Python to PyPI, `test_publish` | **Public, permanent** development release on production PyPI | `0.0.0.devRUN_ID` |
| Publish Direct Dashboard Pages, `test_publish` | **Replaces the live site** | `0.0.0-test.RUN_ID.RUN_ATTEMPT` |

Manual dispatch needs the workflow on the default branch, and the selected branch must contain it too.

## .NET Packaging

[Publish .NET Packages](../../.github/workflows/publish-dotnet.yml) runs inside Release after host validation. It builds
the self-contained archives on native runners, extracts them, and checks CLI startup, version metadata, and a dashboard
smoke test (static assets, session/CSRF, graceful shutdown) without touching any board. To rerun the smoke test locally
against an archive extracted outside the checkout:

```sh
python3 tools/release/check-dotnet-host.py /path/to/extracted-host
```

To test packaging from a branch, run **Actions > Publish .NET Packages**, choose the branch, and enter a base **tag**
such as `v1.2.3`. Download the `release-host-*` artifacts and verify the checksum before use. No credentials are needed.

## PyPI Publication

[Publish Python to PyPI](../../.github/workflows/publish-pypi.yml) runs after a successful tag-push Release. It
downloads the released wheel and checksum and verifies the package name, filename, and version. The unchanged wheel is
then uploaded through the protected `pypi` environment. Stable packages are never rebuilt.

### Trusted Publisher Setup

1. Create the GitHub environment **pypi** (Settings > Environments), optionally with required reviewers. Allow the
   default branch and any refs used for manual runs.
2. On PyPI, open the project's **Publishing** settings, or add a pending publisher for a first upload, and register:

| PyPI field | Value |
| --- | --- |
| Project name | `pico-uart` |
| Owner | `djwinter29-oss` |
| Repository | `PicoUart` |
| Workflow filename | `publish-pypi.yml` |
| Environment | `pypi` |

No API token or secret is needed. Only the upload job gets `id-token: write`, and uploads carry Trusted Publishing
attestations.

### Retry and Development Uploads

To retry, or to publish an older release, run the workflow manually with the published **tag** and `test_publish`
unchecked. Editing a release by hand does not trigger it. For a development upload, check `test_publish` and leave
**tag** empty: the workflow tests and builds a marked wheel and uploads it after approval, and also keeps the wheel as a
workflow artifact.

PyPI versions and filenames are permanent, even after deletion. Check PyPI before retrying, and ship corrections under
a new version. A PyPI failure does not roll back the GitHub Release or Pages.

## Direct Dashboard Pages

[Publish Direct Dashboard Pages](../../.github/workflows/publish-direct.yml) deploys `host/direct/src` after a
successful tag-push Release. It checks out the gate-validated commit, runs the Direct Dashboard browser tests, and
stamps the visible version from the tag. Deployments run one at a time.

Setup: set **Settings > Pages > Source** to **GitHub Actions**, and allow the default branch plus any manual-deploy refs
in the **github-pages** environment.

To redeploy or roll back, run the workflow manually with a published **tag** and `test_publish` unchecked. To test
from a branch, check `test_publish` and leave **tag** empty; that replaces the live site, so rerun with the release tag
afterwards to restore it.

A Pages failure does not unpublish the GitHub Release. After deploying, check the live site with a board.
