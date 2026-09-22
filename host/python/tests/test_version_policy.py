#!/usr/bin/env python3
"""Automated coverage for the PICO_UART_VERSION acceptance/rejection policy
in firmware/version.cmake (extracted from firmware/CMakeLists.txt).

Runs `cmake -P` against a tiny standalone driver script so the version
contract (0-255 per part, major/minor > 99 warns but is still a valid local
build, out-of-range parts are a hard error) is exercised directly without
configuring the full firmware/pico-sdk project — no toolchain, SDK checkout,
or firmware build is required, keeping this fast and redundant-build-free.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]
VERSION_CHECK_SCRIPT = REPO_ROOT / "firmware" / "version.cmake"

pytestmark = pytest.mark.skipif(shutil.which("cmake") is None, reason="cmake not installed")


def _run(version: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["cmake", f"-DPICO_UART_VERSION={version}", "-P", str(VERSION_CHECK_SCRIPT)],
        capture_output=True,
        text=True,
    )


@pytest.mark.parametrize(
    ("version", "major", "minor", "patch", "bcd"),
    [
        ("1.2.3", 1, 2, 3, 0x0102),
        ("99.99.255", 99, 99, 255, 0x9999),
        # major/minor > 99 cannot be BCD-encoded; local/dev builds accept it
        # (with a warning) but release.yml refuses to tag it (0-99 policy).
        ("100.0.0", 100, 0, 0, 0),
        ("255.255.255", 255, 255, 255, 0),
    ],
)
def test_version_policy_accepts_in_range_versions(
    version: str, major: int, minor: int, patch: int, bcd: int
) -> None:
    result = _run(version)

    assert result.returncode == 0, result.stderr
    assert f"major={major} minor={minor} patch={patch} bcd={bcd}" in result.stderr


@pytest.mark.parametrize(
    "version",
    [
        "256.0.0",  # exceeds the hard 0-255 per-part cap, unlike major/minor > 99
        "0.256.0",
        "0.0.256",
        "1.2",
        "1.2.3.4",
        "1.2.3-beta",
    ],
)
def test_version_policy_rejects_out_of_range_or_malformed_versions(version: str) -> None:
    result = _run(version)

    assert result.returncode != 0
    assert "PICO_UART_VERSION" in result.stderr
