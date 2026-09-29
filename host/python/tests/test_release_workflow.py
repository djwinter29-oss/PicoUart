#!/usr/bin/env python3
"""Coverage for the release.yml tag trigger glob and version policy script.

GitHub Actions tag filters are glob patterns, not regexes: '+' has no
"one or more" meaning there. A prior version of the workflow used
"v[0-9]+.[0-9]+.[0-9]+", which only matches tags containing a literal '+'
and never matches a real tag like v1.2.3. These tests pin the fixed glob
and the release-tag version policy enforced by tools/release/resolve-release-version.sh.

They also pin the PR and release firmware matrices: rated pico/pico2 plus
the development overclock images (pico at 250 MHz, pico2 at 300 MHz).
"""

from __future__ import annotations

import fnmatch
import re
import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]
RELEASE_WORKFLOW = REPO_ROOT / ".github" / "workflows" / "release.yml"
PR_WORKFLOW = REPO_ROOT / ".github" / "workflows" / "pr-check.yml"
RESOLVE_SCRIPT = REPO_ROOT / "tools" / "release" / "resolve-release-version.sh"

# board, clock_khz, overclock, label. Rated rows stay on the default clock
# path; overclock rows must pass --unsafe-overclock.
FIRMWARE_MATRIX = (
    ("pico", "125000", "false", "pico"),
    ("pico2", "150000", "false", "pico2"),
    ("pico", "250000", "true", "pico-250mhz"),
    ("pico2", "300000", "true", "pico2-300mhz"),
)


def _firmware_matrix(text: str) -> list[tuple[str, str, str, str]]:
    return re.findall(
        r"- board: (\w+)\n\s+clock_khz: \"(\d+)\"\n\s+overclock: \"(true|false)\"\n\s+label: ([\w-]+)",
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


@pytest.mark.parametrize("workflow", [PR_WORKFLOW, RELEASE_WORKFLOW])
def test_firmware_matrix_builds_rated_and_overclock_images(workflow: Path) -> None:
    text = workflow.read_text(encoding="utf-8")

    assert _firmware_matrix(text) == list(FIRMWARE_MATRIX)
    assert 'if [ "$OVERCLOCK" = "true" ]; then' in text
    assert '--system-clock-khz "$CLOCK_KHZ" --unsafe-overclock' in text
    assert '--board "$BOARD"' in text
    assert '--board "$LABEL"' not in text
    assert "--board ${{ matrix.label }}" not in text


def test_pr_overclock_artifacts_use_distinct_names() -> None:
    text = PR_WORKFLOW.read_text(encoding="utf-8")

    assert "name: pico_uart-${{ matrix.label }}" in text
    assert "build/firmware-${{ matrix.label }}/pico_uart.uf2" in text
    assert "matrix.label == 'pico'" in text


def test_release_overclock_packages_use_distinct_names() -> None:
    text = RELEASE_WORKFLOW.read_text(encoding="utf-8")

    assert "name: release-${{ matrix.label }}" in text
    assert "pico_uart-v${VERSION}-${LABEL}" in text
    assert 'SHA256SUMS-${LABEL}.txt' in text
    assert "pattern: release-*" in text
    assert "pico-250mhz" in text
    assert "pico2-300mhz" in text
