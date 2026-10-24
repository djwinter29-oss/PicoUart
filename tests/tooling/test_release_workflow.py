#!/usr/bin/env python3
"""Coverage for the release.yml tag trigger glob and version policy script.

GitHub Actions tag filters are glob patterns, not regexes: '+' has no
"one or more" meaning there. A prior version of the workflow used
"v[0-9]+.[0-9]+.[0-9]+", which only matches tags containing a literal '+'
and never matches a real tag like v1.2.3. These tests pin the fixed glob
and the release-tag version policy enforced by tools/release/resolve-release-version.sh.

They also pin the PR and release firmware matrices to the rated pico/pico2
images only; overclock selection now lives in the on-device configuration
profile, not workflow matrix variants.
"""

from __future__ import annotations

import fnmatch
import json
import os
import re
import shutil
import subprocess
import textwrap
from zipfile import ZipFile
from pathlib import Path

import pytest
import require_release_checks

REPO_ROOT = Path(__file__).resolve().parents[2]
RELEASE_WORKFLOW = REPO_ROOT / ".github" / "workflows" / "release.yml"
PR_WORKFLOW = REPO_ROOT / ".github" / "workflows" / "pr-check.yml"
RESOLVE_SCRIPT = REPO_ROOT / "tools" / "release" / "resolve-release-version.sh"

# board, label.
PR_FIRMWARE_MATRIX = (
    ("pico", "pico"),
    ("pico2", "pico2"),
)
# ponytail: PR and release targets are intentionally identical today; keep
# separate names to make any future workflow-specific divergence explicit.
RELEASE_FIRMWARE_MATRIX = PR_FIRMWARE_MATRIX


def _firmware_matrix(text: str) -> list[tuple[str, str]]:
    return re.findall(
        r"- board: (\w+)\n\s+label: ([\w-]+)",
        text,
    )


def _tag_glob() -> str:
    # Avoid a PyYAML dependency for a single scalar: extract the quoted
    # glob directly from the "tags:" list item under "on: push:".
    match = re.search(r'tags:\s*\n\s*(?:#.*\n\s*)*-\s*"([^"]+)"', RELEASE_WORKFLOW.read_text(encoding="utf-8"))
    assert match, "expected a single quoted glob under on.push.tags"
    return match.group(1)


@pytest.mark.parametrize(
    "tag",
    ["v1.2.3", "v0.0.0", "v99.99.255", "v10.20.30"],
)
def test_release_tag_glob_matches_real_semver_tags(tag: str) -> None:
    assert fnmatch.fnmatch(tag, _tag_glob())


@pytest.mark.parametrize(
    "tag",
    ["1.2.3", "release-v1.2.3", "vX.Y.Z"],
)
def test_release_tag_glob_rejects_non_v_prefixed_or_non_numeric_tags(tag: str) -> None:
    # The push-trigger glob only needs to be loose enough to reliably fire for
    # real vMAJOR.MINOR.PATCH tags (GitHub tag globs can't express "exactly
    # one dot-delimited numeric group" via '+'). Tags like v1.2.3-rc1 or
    # v1.2.3.4 do match this glob and are rejected later by the job-level
    # "Resolve version" policy in tools/release/resolve-release-version.sh instead
    # (see test_resolve_release_version_rejects_outside_policy).
    assert not fnmatch.fnmatch(tag, _tag_glob())


def _resolve(event_name: str, input_tag: str, ref_name: str) -> subprocess.CompletedProcess:
    # Invoke via "sh" explicitly rather than executing the script path
    # directly: Windows has no shebang support, so running the POSIX
    # script as a bare executable fails with WinError 193 ("%1 is not a
    # valid Win32 application"). Git-for-Windows' sh.exe (on PATH for
    # GitHub-hosted windows-latest runners) and every POSIX sh both honor
    # this invocation the same way.
    return subprocess.run(
        ["sh", str(RESOLVE_SCRIPT), event_name, input_tag, ref_name],
        capture_output=True,
        text=True,
    )


@pytest.mark.parametrize(
    ("ref_name", "expected"),
    [
        ("v1.2.3", "1.2.3"),
        ("v99.99.255", "99.99.255"),
    ],
)
def test_resolve_release_version_accepts_within_policy(ref_name: str, expected: str) -> None:
    result = _resolve("push", "", ref_name)

    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == expected


@pytest.mark.parametrize(
    "ref_name",
    ["v100.0.0", "v255.255.255", "v256.0.0", "v1.2.3-rc1", "v1.2"],
)
def test_resolve_release_version_rejects_outside_policy(ref_name: str) -> None:
    result = _resolve("push", "", ref_name)

    assert result.returncode != 0
    assert "Refusing version" in result.stderr


def test_resolve_release_version_uses_workflow_dispatch_input() -> None:
    result = _resolve("workflow_dispatch", "v1.2.3", "refs/heads/main")

    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "1.2.3"


@pytest.mark.parametrize("event", ["push", "workflow_dispatch"])
@pytest.mark.parametrize("tag", ["", "1.2.3", "vv1.2.3", "v01.2.3", "v1.2.3-rc1", "v1.2"])
def test_release_versions_require_consistent_v_prefixed_tags(event, tag):
    result = _resolve(event, tag, tag)
    assert result.returncode != 0
    assert "expected tag vMAJOR.MINOR.PATCH" in result.stderr


@pytest.mark.parametrize("filename", ["release.yml", "publish-dotnet.yml", "publish-pypi.yml", "publish-webhid.yml"])
def test_manual_release_inputs_use_the_same_tag_contract(filename):
    text = (REPO_ROOT / ".github/workflows" / filename).read_text(encoding="utf-8")
    inputs = text.split("  workflow_dispatch:\n    inputs:\n", 1)[1].split("\npermissions:", 1)[0]
    assert "      tag:\n" in inputs
    assert "vMAJOR.MINOR.PATCH" in inputs
    assert "      version:\n" not in inputs
    assert "inputs.version" not in text


@pytest.mark.parametrize(
    ("workflow", "expected"),
    [
        (PR_WORKFLOW, PR_FIRMWARE_MATRIX),
        (RELEASE_WORKFLOW, RELEASE_FIRMWARE_MATRIX),
    ],
)
def test_firmware_matrix_builds_rated_and_overclock_images(workflow: Path, expected) -> None:
    text = workflow.read_text(encoding="utf-8")

    assert _firmware_matrix(text) == list(expected)
    assert "--system-clock-khz" not in text
    assert "--unsafe-overclock" not in text
    assert '--board "$BOARD"' in text
    assert '--board "$LABEL"' not in text
    assert "--board ${{ matrix.label }}" not in text


def test_pr_artifacts_use_matrix_label_names() -> None:
    text = PR_WORKFLOW.read_text(encoding="utf-8")

    assert "name: pico_uart-${{ matrix.label }}" in text
    assert "build/firmware-${{ matrix.label }}/pico_uart.uf2" in text
    assert "matrix.label == 'pico'" in text


def test_host_apps_validate_python_and_dotnet_on_all_platforms() -> None:
    text = (REPO_ROOT / ".github" / "workflows" / "host-validation.yml").read_text(encoding="utf-8")
    job = text.split("\n  host-apps:\n", 1)[1].split("\n  host-tools:\n", 1)[0]

    assert "os: [ubuntu-latest, macos-latest, windows-latest]" in job
    assert "fail-fast: false" in job
    assert 'dotnet test host/dotnet/PicoUart.sln --filter "Category=Unit"' in job
    assert "python -m pytest -c pyproject.toml" in job
    assert "if: runner.os == 'Windows'" in job
    assert "tools/validation/run-host-tests.sh --skip-python" in job
    linux_job = text.split("\n  host-tools:\n", 1)[1].split("\n  host-python-minimum:\n", 1)[0]
    assert "runs-on: ubuntu-latest" in linux_job
    assert "tools/validation/run-host-tests.sh --sanitize --skip-python" in linux_job
    assert "\n  host-tools-windows:" not in text
    assert "ubuntu-24.04" not in text


def test_release_packages_use_distinct_names() -> None:
    text = RELEASE_WORKFLOW.read_text(encoding="utf-8")

    assert "name: release-${{ matrix.label }}" in text
    assert "pico_uart-v${VERSION}-${LABEL}" in text
    assert "SHA256SUMS-${LABEL}.txt" in text
    assert "pattern: release-*" in text


def test_tagged_host_packages_embed_the_release_version_and_gate_publication() -> None:
    text = RELEASE_WORKFLOW.read_text(encoding="utf-8")
    job = (REPO_ROOT / ".github/workflows/publish-dotnet.yml").read_text(encoding="utf-8")
    for rid in ("linux-x64", "win-x64", "osx-arm64"):
        assert f"rid: {rid}" in job
    assert "version.txt" not in job
    assert "PicoUart.deps.json" in job
    assert "workflow_call:" in job
    assert "workflow_dispatch:" in job
    assert "contents: write" not in job
    assert "pip wheel" not in job
    python_job = text.split("\n  python-package:\n", 1)[1].split("\n  publish:\n", 1)[0]
    assert "fetch-depth: 0" in python_job
    assert "SETUPTOOLS_SCM_PRETEND_VERSION:" in python_job
    assert "SETUPTOOLS_SCM_PRETEND_VERSION: ${{ steps.version.outputs.version }}" in python_job
    assert "PICO_UART_HOST_VERSION:" not in python_job
    assert "--self-contained true" in job
    assert '-p:Version="$VERSION"' in job
    assert "Verify installed Python release" in text
    assert "SHA256SUMS-python.txt" in text
    assert "name: release-python" in text
    assert "name: release-host-${{ matrix.rid }}" in job
    assert "uses: ./.github/workflows/publish-dotnet.yml" in text
    assert "tag: ${{ inputs.tag || '' }}" in text
    assert "needs: [build, host-validation, python-package, dotnet-packages]" in text
    publish = text.split("\n  publish:\n", 1)[1]
    assert 'require_release_checks.py --sha "$SOURCE_SHA"' in publish
    assert publish.index("Require successful PR checks") < publish.index("- name: Publish GitHub Release")
    assert "draft: false" in publish
    assert "prerelease: false" in publish
    assert "draft: true" not in publish
    assert "Hardware qualification is separate" in publish


def test_python_test_publication_overrides_git_version():
    text = (REPO_ROOT / ".github/workflows/publish-pypi.yml").read_text(encoding="utf-8")
    assert "SETUPTOOLS_SCM_PRETEND_VERSION: 0.0.0.dev${{ github.run_id }}" in text
    assert "PICO_UART_HOST_VERSION:" not in text


@pytest.mark.parametrize("workflow,step_name", [
    ("release.yml", "Verify installed Python release"),
    ("release.yml", "Checksum Python wheel"),
    ("publish-dotnet.yml", "Check and archive standalone host"),
    ("publish-pypi.yml", "Verify installed development wheel"),
])
def test_host_release_embedded_python_compiles(workflow, step_name):
    text = (REPO_ROOT / ".github/workflows" / workflow).read_text(encoding="utf-8")
    step = text.split(f"- name: {step_name}\n", 1)[1].split("\n      - name:", 1)[0]
    script = textwrap.dedent(step.split("<<'PY'\n", 1)[1].rsplit("\n          PY", 1)[0])
    compile(script, step_name, "exec")


def test_pypi_publication_reuses_released_artifacts_with_scoped_oidc():
    text = (REPO_ROOT / ".github/workflows/publish-pypi.yml").read_text(encoding="utf-8")
    verify, publish = text.split("\n  publish:\n", 1)
    publish = publish.split("\n  test-package:\n", 1)[0]
    assert "workflows: [Release]" in verify
    assert "types: [completed]" in verify
    assert ".isDraft == false and .isPrerelease == false" in verify
    assert "gh release download" in verify
    assert 'require_release_checks.py --tag "$RELEASE_TAG"' in verify
    assert verify.index("require_release_checks.py") < verify.index("gh release download")
    assert "actions: read" in verify
    assert "SHA256SUMS-python.txt" in verify
    assert "id-token: write" not in verify
    assert "needs: verify" in publish
    assert "id-token: write" in publish
    assert "name: pypi" in publish
    assert "repository-url: https://upload.pypi.org/legacy/" in publish
    assert "test.pypi.org" not in publish
    assert "pypa/gh-action-pypi-publish@" in publish
    assert "password:" not in text
    assert "pip wheel" not in verify + publish
    assert "skip-existing:" not in text


def test_manual_python_publication_uses_marked_versions_on_production_pypi():
    text = (REPO_ROOT / ".github/workflows/publish-pypi.yml").read_text(encoding="utf-8")
    production, test = text.split("\n  test-package:\n", 1)
    assert "(github.event_name != 'workflow_dispatch' || !inputs.test_publish)" in production
    assert "if: github.event_name == 'workflow_dispatch' && inputs.test_publish" in test
    assert "0.0.0.dev${{ github.run_id }}" in test
    assert "Run Python package tests" in test
    assert "Verify installed development wheel" in test
    assert "needs: test-package" in test
    assert "name: pypi\n" in test
    assert "repository-url: https://upload.pypi.org/legacy/" in test
    assert "name: pypi-development-distribution" in test
    assert "public, immutable development version" in production
    assert "testpypi" not in text.lower()
    assert "test.pypi.org" not in text
    assert "require_release_checks.py" not in test


def test_pypi_publishers_pin_the_commit_not_the_annotated_release_tag():
    text = (REPO_ROOT / ".github/workflows/publish-pypi.yml").read_text(encoding="utf-8")
    refs = re.findall(r"uses: pypa/gh-action-pypi-publish@([0-9a-f]{40})", text)
    assert refs == ["7245906fb61fa33a13554f9d5fb856fe61758086"] * 2


@pytest.mark.parametrize("event,test_publish,reference,expected", [
    ("workflow_dispatch", "true", "main", "1.2.3-test.123.2"),
    ("workflow_dispatch", "false", "main", "1.2.3"),
    ("push", "false", "v1.2.3", "1.2.3"),
])
def test_dotnet_manual_versions_are_marked_without_changing_release_versions(
        tmp_path, event, test_publish, reference, expected):
    text = (REPO_ROOT / ".github/workflows/publish-dotnet.yml").read_text(encoding="utf-8")
    step = text.split("- name: Resolve host version\n", 1)[1].split("\n      - name:", 1)[0]
    script = textwrap.dedent(step.split("run: |\n", 1)[1])
    output = tmp_path / "output"
    result = subprocess.run(["sh"], input=script, text=True, capture_output=True, cwd=REPO_ROOT,
                            env={**os.environ, "EVENT_NAME": event, "INPUT_TAG": "v1.2.3", "REF_NAME": reference,
                                 "TEST_PUBLISH": test_publish, "GITHUB_RUN_ID": "123", "GITHUB_RUN_ATTEMPT": "2",
                                 "GITHUB_OUTPUT": str(output)})
    assert result.returncode == 0, result.stdout + result.stderr
    assert output.read_text().strip() == f"version={expected}"
    assert "github.event_name == 'workflow_dispatch' && !inputs.release_build" in step


@pytest.mark.parametrize("case", ["valid", "checksum", "version", "name", "missing", "extra"])
def test_pypi_wheel_verification_rejects_invalid_release_artifacts(tmp_path, case):
    import hashlib
    import sys

    text = (REPO_ROOT / ".github/workflows/publish-pypi.yml").read_text(encoding="utf-8")
    step = text.split("- name: Verify wheel checksum and metadata\n", 1)[1].split("\n      - name:", 1)[0]
    script = textwrap.dedent(step.split("<<'PY'\n", 1)[1].rsplit("\n          PY", 1)[0])
    incoming = tmp_path / "incoming"
    incoming.mkdir()
    wheel = incoming / "pico_uart-1.2.3-py3-none-any.whl"
    name = "other-package" if case == "name" else "pico-uart"
    version = "9.9.9" if case == "version" else "1.2.3"
    with ZipFile(wheel, "w") as archive:
        archive.writestr("pico_uart-1.2.3.dist-info/METADATA", f"Name: {name}\nVersion: {version}\n")
    digest = "0" * 64 if case == "checksum" else hashlib.sha256(wheel.read_bytes()).hexdigest()
    (incoming / "SHA256SUMS-python.txt").write_text(f"{digest}  {wheel.name}\n", encoding="utf-8")
    if case == "missing":
        wheel.unlink()
    elif case == "extra":
        (incoming / "extra.whl").write_bytes(wheel.read_bytes())
    result = subprocess.run([sys.executable, "-"], input=script, text=True, capture_output=True,
                            cwd=tmp_path, env={**os.environ, "VERSION": "1.2.3"})
    assert (result.returncode == 0) == (case == "valid"), result.stderr
    if case == "valid":
        assert (tmp_path / "dist" / wheel.name).read_bytes() == wheel.read_bytes()
    else:
        assert not (tmp_path / "dist").exists()


def test_webhid_pages_requires_a_published_release_and_checks_the_tag() -> None:
    text = (REPO_ROOT / ".github/workflows/publish-webhid.yml").read_text(encoding="utf-8")

    assert "workflows: [Release]" in text
    assert "types: [completed]" in text
    assert "workflow_dispatch:" in text
    assert "tools/release/resolve-release-version.sh push '' \"$RELEASE_TAG\"" in text
    assert "gh release view" in text
    assert ".isDraft == false and .isPrerelease == false" in text
    assert "ref: ${{ steps.checks.outputs.sha || github.sha }}" in text
    assert "node --test host/webhid/tests/*.test.mjs" in text
    assert text.index("Validate published release") < text.index("Checkout WebHID")
    assert text.index("Check WebHID") < text.index("Upload WebHID site")


def test_webhid_pages_test_publish_is_explicit_and_uses_the_dispatched_commit() -> None:
    text = (REPO_ROOT / ".github/workflows/publish-webhid.yml").read_text(encoding="utf-8")
    inputs = text.split("    inputs:\n", 1)[1].split("\npermissions:", 1)[0]

    assert "test_publish:" in inputs
    assert "default: false" in inputs
    assert "type: boolean" in inputs
    section = text.split("- name: Validate published release\n", 1)[1].split("\n      - name:", 1)[0]
    assert "if: github.event_name != 'workflow_dispatch' || !inputs.test_publish" in section
    assert 'require_release_checks.py --tag "$RELEASE_TAG"' in text
    assert text.index("Require successful tests") < text.index("Checkout WebHID")
    gate = text.split("- name: Require successful tests\n", 1)[1].split("\n      - name:", 1)[0]
    assert "if: github.event_name != 'workflow_dispatch' || !inputs.test_publish" in gate
    assert "ref: ${{ steps.checks.outputs.sha || github.sha }}" in text


@pytest.mark.parametrize("version,success", [
    ("0.0.0-debug", True), ("1.2.3", True), ("0.0.0-test.123.1", True), ("<script>", False),
])
def test_webhid_pages_stamps_only_valid_host_versions(tmp_path, version, success):
    node = shutil.which("node")
    if not node:
        pytest.skip("Node unavailable")
    text = (REPO_ROOT / ".github/workflows/publish-webhid.yml").read_text(encoding="utf-8")
    step = text.split("- name: Stamp WebHID version\n", 1)[1].split("\n      - name:", 1)[0]
    script = textwrap.dedent(step.split("<<'JS'\n", 1)[1].rsplit("\n          JS", 1)[0])
    target = tmp_path / "host/webhid/index.html"
    target.parent.mkdir(parents=True)
    original = (REPO_ROOT / "host/webhid/index.html").read_text(encoding="utf-8")
    target.write_text(original, encoding="utf-8")
    result = subprocess.run([node, "--input-type=module", "-"], input=script, text=True,
                            capture_output=True, cwd=tmp_path, env={**os.environ, "HOST_VERSION": version})
    assert (result.returncode == 0) == success, result.stderr
    expected = original.replace(">0.0.0-debug</span>", f">{version}</span>") if success else original
    assert target.read_text(encoding="utf-8") == expected
    assert "format('0.0.0-test.{0}.{1}', github.run_id, github.run_attempt)" in step


def test_webhid_pages_deploys_only_the_independent_site_with_scoped_permissions() -> None:
    text = (REPO_ROOT / ".github/workflows/publish-webhid.yml").read_text(encoding="utf-8")
    build, deploy = text.split("\n  deploy:\n", 1)

    assert "path: host/webhid" in build
    assert "path: host/web\n" not in text
    assert "needs: build" in deploy
    assert "pages: write" not in build
    assert "id-token: write" not in build
    assert "pages: write" in deploy
    assert "id-token: write" in deploy
    assert "name: github-pages" in deploy
    assert "group: webhid-pages" in text
    assert "cancel-in-progress: false" in text


@pytest.mark.parametrize("status,conclusion", [
    ("completed", "failure"), ("completed", "cancelled"), ("completed", "skipped"),
    ("in_progress", None), ("queued", None),
])
def test_publication_does_not_accept_an_older_success(monkeypatch, status, conclusion):
    runs = [
        {"head_sha": "abc", "event": "push", "head_branch": "v1.2.3", "run_number": 1, "run_attempt": 1,
         "status": "completed", "conclusion": "success", "html_url": "https://example.test/1"},
        {"head_sha": "abc", "event": "push", "head_branch": "v1.2.3", "run_number": 2, "run_attempt": 1,
         "status": status, "conclusion": conclusion, "html_url": "https://example.test/2"},
    ]
    monkeypatch.setattr(require_release_checks.subprocess, "run", lambda *args, **kwargs:
                        subprocess.CompletedProcess(args, 0, stdout=json.dumps([{"workflow_runs": runs}])))
    with pytest.raises(RuntimeError, match="publication blocked"):
        require_release_checks.require_successful_run("owner/repo", "release.yml", "abc", "v1.2.3")


@pytest.mark.parametrize("sha,tag,success", [
    ("abc", "v1.2.3", True), ("other", "v1.2.3", False), ("abc", "v2.0.0", False),
])
def test_publication_requires_tests_for_the_exact_commit_and_tag(monkeypatch, sha, tag, success):
    import json

    commands = []
    run = {"head_sha": "abc", "event": "push", "head_branch": "v1.2.3", "run_number": 1, "run_attempt": 1,
           "status": "completed", "conclusion": "success", "html_url": "https://example.test/1"}
    def gh_api(command, **kwargs):
        commands.append(command)
        return subprocess.CompletedProcess(command, 0, stdout=json.dumps([{"workflow_runs": [run]}]))

    monkeypatch.setattr(require_release_checks.subprocess, "run", gh_api)
    if success:
        require_release_checks.require_successful_run("owner/repo", "release.yml", sha, tag)
        assert commands[0][-1] == "repos/owner/repo/actions/workflows/release.yml/runs"
        first_filter = commands[0].index("-F")
        assert commands[0][first_filter:first_filter + 2] == ["-F", f"head_sha={sha}"]
        page_size = commands[0].index("-F", first_filter + 1)
        assert commands[0][page_size:page_size + 2] == ["-F", "per_page=100"]
    else:
        with pytest.raises(RuntimeError, match="no required test run"):
            require_release_checks.require_successful_run("owner/repo", "release.yml", sha, tag)


@pytest.mark.parametrize("mode,failed_workflow", [
    ("--tag", None), ("--sha", None), ("--tag", "pr-check.yml"), ("--tag", "release.yml"),
])
def test_publication_gate_requires_all_applicable_workflows(monkeypatch, tmp_path, mode, failed_workflow):
    import sys

    calls = []
    output = tmp_path / "output"
    monkeypatch.setenv("GITHUB_REPOSITORY", "owner/repo")
    monkeypatch.setenv("GITHUB_OUTPUT", str(output))
    monkeypatch.setattr(sys, "argv", ["require_release_checks.py", mode, "v1.2.3" if mode == "--tag" else "abc"])

    def gh_api(command, **kwargs):
        endpoint = command[-1]
        calls.append(endpoint)
        if "/commits/" in endpoint:
            payload = {"sha": "abc"}
        else:
            run = {"head_sha": "abc", "event": "push", "head_branch": "v1.2.3", "run_number": 1,
                   "run_attempt": 1, "status": "completed", "conclusion": "success",
                   "html_url": "https://example.test/1"}
            if failed_workflow and failed_workflow in endpoint:
                run["conclusion"] = "failure"
            payload = [{"workflow_runs": [run]}]
        return subprocess.CompletedProcess(command, 0, stdout=json.dumps(payload))

    monkeypatch.setattr(require_release_checks.subprocess, "run", gh_api)
    assert require_release_checks.main() == (1 if failed_workflow else 0)
    if failed_workflow:
        assert not output.exists()
    else:
        assert output.read_text() == "sha=abc\n"
        assert any("pr-check.yml" in call for call in calls)
        assert any("release.yml" in call for call in calls) == (mode == "--tag")


@pytest.mark.parametrize("filename,job_name", [
    ("publish-pypi.yml", "verify"), ("publish-webhid.yml", "build"),
])
def test_publishers_follow_successful_same_repository_tag_builds(filename, job_name):
    text = (REPO_ROOT / ".github/workflows" / filename).read_text(encoding="utf-8")
    assert "  workflow_run:\n    workflows: [Release]\n    types: [completed]" in text
    assert "  release:\n" not in text
    job = text.split(f"\n  {job_name}:\n", 1)[1].split("    runs-on:", 1)[0]
    assert "github.event.workflow_run.conclusion == 'success' &&" in job
    assert "github.event.workflow_run.event == 'push' &&" in job
    assert "github.event.workflow_run.head_repository.full_name == github.repository" in job
    assert "RELEASE_TAG: ${{ github.event.workflow_run.head_branch || inputs.tag }}" in text
    assert "SOURCE_SHA: ${{ github.event.workflow_run.head_sha }}" in text
    assert '--tag "$RELEASE_TAG" --expected-sha "$SOURCE_SHA"' in text
    assert "github.event.release" not in text


@pytest.mark.parametrize("expected_sha,success", [("abc", True), ("other", False)])
def test_publication_gate_checks_triggering_commit_before_workflow_validation(
        monkeypatch, tmp_path, expected_sha, success):
    import sys

    calls = []
    output = tmp_path / "output"
    monkeypatch.setenv("GITHUB_REPOSITORY", "owner/repo")
    monkeypatch.setenv("GITHUB_OUTPUT", str(output))
    monkeypatch.setattr(sys, "argv", ["require_release_checks.py", "--tag", "v1.2.3",
                                     "--expected-sha", expected_sha])

    def gh_api(command, **kwargs):
        endpoint = command[-1]
        calls.append(endpoint)
        if "/commits/" in endpoint:
            payload = {"sha": "abc"}
        else:
            run = {"head_sha": "abc", "event": "push", "head_branch": "v1.2.3", "run_number": 1,
                   "run_attempt": 1, "status": "completed", "conclusion": "success",
                   "html_url": "https://example.test/1"}
            payload = [{"workflow_runs": [run]}]
        return subprocess.CompletedProcess(command, 0, stdout=json.dumps(payload))

    monkeypatch.setattr(require_release_checks.subprocess, "run", gh_api)
    assert require_release_checks.main() == (0 if success else 1)
    if success:
        assert output.read_text() == "sha=abc\n"
        assert len(calls) == 3
    else:
        assert not output.exists()
        assert len(calls) == 1


def test_publication_gate_blocks_when_github_api_is_unavailable(monkeypatch):
    import sys

    monkeypatch.setenv("GITHUB_REPOSITORY", "owner/repo")
    monkeypatch.setattr(sys, "argv", ["require_release_checks.py", "--tag", "v1.2.3"])

    def unavailable(command, **kwargs):
        raise subprocess.CalledProcessError(1, command)

    monkeypatch.setattr(require_release_checks.subprocess, "run", unavailable)
    assert require_release_checks.main() == 1


def test_release_notes_reference_config_profiles() -> None:
    text = RELEASE_WORKFLOW.read_text(encoding="utf-8")
    body = text.split("          body: |", 1)[1].split("        env:", 1)[0]
    assert "rated **pico** (125 MHz) and **pico2** (150 MHz)" in body
    assert "Optional overclock profiles are selected on-device" in body
    assert "Some Pico 2 boards may not support 300 MHz reliably" in body
