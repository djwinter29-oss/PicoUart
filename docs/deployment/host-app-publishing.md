# Host App Publishing

This runbook covers .NET archive test runs, Python publication to PyPI, and WebHID deployment to GitHub Pages.
For channels and supported downloads, see [Host App Distribution](host-app-distribution.md). For the release sequence,
tag policy, and exact-artifact qualification, start with [Releasing PicoUart](releasing.md).

Normal publication requires the [shared publication gates](releasing.md#publication-gates). The explicit manual test
modes bypass the full cross-project gate but still run scoped checks and remain subject to environment restrictions:

- .NET manual tests produce marked workflow artifacts, not public release attachments.
- Python development tests upload **public, immutable versions to production PyPI**, not a private preview index.
- Pages test deployments **replace the live site**; they do not create an isolated preview.

## .NET Packaging

The [Publish .NET Packages workflow](../../.github/workflows/publish-dotnet.yml) is called by Release after host
validation. It builds self-contained archives, verifies extraction, native CLI startup, and version metadata, and
returns archives and per-target checksums for the release draft. It does not create its own GitHub Release.
The [distribution guide](host-app-distribution.md#net-downloads) owns supported targets and installation boundaries.

For an independent packaging test:

1. Commit and push the desired changes. The workflow must exist on the default branch for manual dispatch, and the
   branch selected for the run must contain it.
2. Open **Actions > Publish .NET Packages > Run workflow** and select that branch.
3. Enter the base **tag**, such as `v1.2.3`. This is a version label, not a requirement that the Git tag already exists;
   it does not change the checked-out branch or create a tag.
4. Download the `release-host-*` workflow artifacts after the native matrix succeeds. Extract the artifact bundle to
   obtain the platform archive and checksum manifest, then verify the checksum before use.

Direct manual runs use the [test-version marker](releasing.md#publication-gates). They do not attach files to an
existing release, promote firmware, publish to PyPI, or change Pages. No publishing credentials or protected publishing
environment is required for these downloadable test artifacts. User installation and local publishing commands remain
in the [.NET host guide](../../host/dotnet/README.md).

## PyPI Publication

The [PyPI workflow](../../.github/workflows/publish-pypi.yml) normally runs when a non-prerelease GitHub Release is
published. It downloads the existing wheel and `SHA256SUMS-python.txt`, verifies the checksum, package name, filename,
and tag version, then passes the unchanged wheel to the protected publishing job. It does not rebuild stable packages
or upload on tag push, draft creation, or release preflight.

All uploads target production PyPI and use environment `pypi`. Source archives have rebuild regression coverage, but
are not uploaded by the current workflow; enabling them needs explicit packaging/publication support and qualification.

### Trusted Publisher Setup

1. Merge the publishing workflow into the default branch.
2. Create GitHub environment **pypi** under **Settings > Environments**, with required reviewers. Permit release tags
   and the approved branches used for manual retry or development publication.
3. Sign in to PyPI with a verified account and two-factor authentication. For an existing project you own, use its
   **Publishing** settings. For a first upload, use **Your account > Publishing > Add a new pending publisher**.
   Confirm project-name availability; a missing public project does not guarantee that you can register it.
4. Register these exact GitHub publisher values:

| PyPI Field | Value |
| --- | --- |
| Project name (pending publisher) | `pico-uart` |
| Owner | `djwinter29-oss` |
| Repository | `PicoUart` |
| Workflow filename | `publish-pypi.yml` |
| Environment | `pypi` |

The filename is not the workflow's display name or full path. If a publisher predates the workflow rename, update its
registration before uploading. Both stable and development modes use this publisher; no PyPI API token, GitHub secret,
separate index, or second publisher is needed. Only upload jobs receive `id-token: write`; verification runs without
publication credentials. The PyPA action creates publication attestations using Trusted Publishing.

### Publish And Retry

1. Create a normal release tag and wait for the gated draft and its wheel/checksum assets.
2. Complete [release HIL qualification](releasing.md#release-hil-gates), publish the qualified draft, and approve the
   **pypi** environment deployment.
3. Verify the published version and files at <https://pypi.org/project/pico-uart/>.

For a failed publication or an older qualified release, run **Actions > Publish Python to PyPI > Run workflow** with
the already-published **tag**, leaving **test_publish** unchecked. The release must contain the expected wheel and
checksum manifest and satisfy the shared gates. Drafts, prereleases, checksum failures, and metadata mismatches fail
before upload.

Publication through another workflow's default `GITHUB_TOKEN` does not trigger the release-event workflow; use manual
dispatch in that case. PyPI versions and uploaded filenames are immutable, even after deletion. Existing distributions
are not silently skipped: inspect PyPI before retrying, and publish corrections under a new version. A PyPI failure
does not roll back GitHub Releases or Pages.

### Manual Development Publication

1. Ensure environment **pypi** permits the selected branch and review its contents before publishing.
2. Open **Actions > Publish Python to PyPI > Run workflow**, select the branch, check **test_publish**, and leave
   **tag** empty.
3. The workflow runs Python package tests, builds the marked development wheel, and checks an installation outside
   the checkout. Approve the protected deployment if required.
4. Verify the public development release on PyPI. The wheel is also retained as the workflow's
   `pypi-development-distribution` artifact.

These are real public uploads using the [development-version scheme](releasing.md#publication-gates), not private
previews or HIL qualification. The first upload can create the project and activate a pending publisher. A new workflow
run gets a new version; rerunning the same run retains its version and fails if that filename was already uploaded.

## WebHID Pages

The [Pages workflow](../../.github/workflows/publish-webhid.yml) publishes the independent app from `host/webhid`.
It neither deploys nor modifies the shared Python/.NET frontend. Normal deployment follows a published,
non-prerelease GitHub Release; a tag push, draft, or release preflight does not change the live site.

### Repository Setup

1. Merge the Pages workflow into the default branch.
2. Under **Settings > Pages > Build and deployment**, set **Source** to **GitHub Actions**. No deployment branch,
   custom domain, or personal access token is required.
3. Configure environment **github-pages** to permit release tags and approved manual-deployment branches. Add required
   reviewers if desired. Only the deployment job receives `pages: write` and `id-token: write`.

The project's hosted dashboard is <https://djwinter29-oss.github.io/PicoUart/>. The deployment environment URL is
authoritative if Pages settings or the repository name changes.

### Publish Or Roll Back

Publish the qualified draft through GitHub's release UI. The Pages workflow validates the published release and shared
gates, checks out the gate-validated commit rather than relying on a movable tag, and runs WebHID checks. It stamps the
visible app version from the tag without its leading `v`, then uploads only `host/webhid`. Deployments are serialized.

For redeployment or rollback, use **Actions > Publish WebHID Pages > Run workflow** with the already-published **tag**
and **test_publish** unchecked. Drafts, prereleases, and tags outside the release policy are rejected. The selected
commit must contain the standalone app and tests. A deployment changes the hosted app, not firmware or connected boards.
If another workflow published the release with its default `GITHUB_TOKEN`, use this manual route instead of expecting
the release event to trigger automatically.

### Test Publish From A Branch

1. Commit and push the desired app and workflow changes. Manual dispatch requires the workflow on the default branch;
   the selected branch must contain the test-publish option.
2. Open **Actions > Publish WebHID Pages > Run workflow** and select that branch.
3. Check **test_publish**, leave **tag** empty, and start the run. Ensure **github-pages** permits the branch and
   approve deployment if required.
4. Inspect the deployment environment URL. To restore a release, rerun with **test_publish** unchecked and its tag.

Test mode deploys the selected commit after scoped WebHID checks, without published-release lookup or the full gate.
It replaces the normal Pages site and shows the [test-version marker](releasing.md#publication-gates); uncommitted
changes are not included. Local WebHID uses `0.0.0-debug`, independent of the connected board's firmware version.

A Pages failure does not unpublish a GitHub Release. Inspect the environment's latest successful deployment, fix or
rerun separately, and check asset loading and browser/device access after deployment. Mocked checks do not prove native
USB compatibility; browser requirements and permissions remain in the [WebHID guide](../../host/webhid/README.md).