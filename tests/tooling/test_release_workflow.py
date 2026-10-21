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
import os
import re
import shutil
import subprocess
import textwrap
from pathlib import Path

import pytest

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


def _resolve(event_name: str, input_version: str, ref_name: str) -> subprocess.CompletedProcess:
    # Invoke via "sh" explicitly rather than executing the script path
    # directly: Windows has no shebang support, so running the POSIX
    # script as a bare executable fails with WinError 193 ("%1 is not a
    # valid Win32 application"). Git-for-Windows' sh.exe (on PATH for
    # GitHub-hosted windows-latest runners) and every POSIX sh both honor
    # this invocation the same way.
    return subprocess.run(
        ["sh", str(RESOLVE_SCRIPT), event_name, input_version, ref_name],
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
    result = _resolve("workflow_dispatch", "1.2.3", "refs/heads/main")

    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "1.2.3"


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
    job = text.split("\n  host-packages:\n", 1)[1].split("\n  publish:\n", 1)[0]
    for rid in ("linux-x64", "win-x64", "osx-arm64"):
        assert f"rid: {rid}" in job
    assert "version.txt" not in job
    assert "PicoUart.deps.json" in job
    assert "PICO_UART_HOST_VERSION: ${{ steps.version.outputs.version }}" in job
    assert "--self-contained true" in job
    assert '-p:Version="$VERSION"' in job
    assert "Verify installed Python release" in job
    assert "name: release-host-${{ matrix.rid }}" in job
    assert "needs: [build, host-validation, host-packages]" in text


@pytest.mark.parametrize("step_name", ["Verify installed Python release", "Check and archive standalone host"])
def test_host_release_embedded_python_compiles(step_name):
    text = RELEASE_WORKFLOW.read_text(encoding="utf-8")
    step = text.split(f"- name: {step_name}\n", 1)[1].split("\n      - name:", 1)[0]
    script = textwrap.dedent(step.split("<<'PY'\n", 1)[1].rsplit("\n          PY", 1)[0])
    compile(script, step_name, "exec")


def test_webhid_pages_requires_a_published_release_and_checks_the_tag() -> None:
    text = (REPO_ROOT / ".github/workflows/webhid-pages.yml").read_text(encoding="utf-8")

    assert "types: [published]" in text
    assert "workflow_dispatch:" in text
    assert "tools/release/resolve-release-version.sh push '' \"$RELEASE_TAG\"" in text
    assert "gh release view" in text
    assert ".isDraft == false and .isPrerelease == false" in text
    assert "ref: ${{ inputs.test_publish && github.sha || github.event.release.tag_name || inputs.tag }}" in text
    assert "node --test host/webhid/tests/*.test.mjs" in text
    assert text.index("Validate published release") < text.index("Checkout WebHID")
    assert text.index("Check WebHID") < text.index("Upload WebHID site")


def test_webhid_pages_test_publish_is_explicit_and_uses_the_dispatched_commit() -> None:
    text = (REPO_ROOT / ".github/workflows/webhid-pages.yml").read_text(encoding="utf-8")
    inputs = text.split("    inputs:\n", 1)[1].split("\npermissions:", 1)[0]

    assert "test_publish:" in inputs
    assert "default: false" in inputs
    assert "type: boolean" in inputs
    for step in ("Checkout release tooling", "Validate published release"):
        section = text.split(f"- name: {step}\n", 1)[1].split("\n      - name:", 1)[0]
        assert "if: github.event_name != 'workflow_dispatch' || !inputs.test_publish" in section
    assert "inputs.test_publish && github.sha" in text


@pytest.mark.parametrize("version,success", [("0.0.0-debug", True), ("1.2.3", True), ("<script>", False)])
def test_webhid_pages_stamps_only_valid_host_versions(tmp_path, version, success):
    node = shutil.which("node")
    if not node:
        pytest.skip("Node unavailable")
    text = (REPO_ROOT / ".github/workflows/webhid-pages.yml").read_text(encoding="utf-8")
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
    assert "inputs.test_publish && '0.0.0-debug' || steps.release.outputs.version" in step


def test_webhid_pages_deploys_only_the_independent_site_with_scoped_permissions() -> None:
    text = (REPO_ROOT / ".github/workflows/webhid-pages.yml").read_text(encoding="utf-8")
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


def test_release_notes_reference_config_profiles() -> None:
    text = RELEASE_WORKFLOW.read_text(encoding="utf-8")
    body = text.split("          body: |", 1)[1].split("        env:", 1)[0]
    assert "rated **pico** (125 MHz) and **pico2** (150 MHz)" in body
    assert "Optional overclock profiles are selected on-device" in body
    assert "Some Pico 2 boards may not support 300 MHz reliably" in body
