# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""The optional hidapi fallback must preserve other hashed requirements."""

import subprocess
import sys

import pytest

FILTER_SCRIPT = "tools/validation/filter_lock_exclude.py"


def _run_filter(repo_root, lock_text: str, exclude: str) -> str:
    script_path = repo_root / FILTER_SCRIPT
    result = subprocess.run(
        [sys.executable, str(script_path), "--exclude", exclude, "-"],
        input=lock_text,
        text=True,
        capture_output=True,
        check=True,
    )
    return result.stdout


def test_hidapi_filter_retains_setuptools_needed_by_vcs_builds(repo_root):
    lock = (repo_root / "host/python/requirements-lock.txt").read_text()
    filtered = _run_filter(repo_root, lock, "hidapi")

    assert "\nhidapi==" not in ("\n" + filtered)
    assert "\nsetuptools==" in ("\n" + filtered)
    # via-hidapi comment must not survive as a dangling reference either.
    assert "# via hidapi" not in filtered

    original_count = lock.count("# via")
    filtered_count = filtered.count("# via")
    assert filtered_count == original_count - 1

    # Every other locked package (and its hashes) must be preserved verbatim.
    for package in (
        "pytest==9.1.1",
        "iniconfig==2.3.0",
        "pluggy==1.6.0",
        "packaging==26.3",
        "exceptiongroup==1.3.1",
        "tomli==2.4.1",
        "typing-extensions==4.16.0",
        "colorama==0.4.6",
    ):
        assert package in filtered
        assert package in lock  # sanity: package really exists upstream


@pytest.mark.parametrize("hashes", [True, False])
def test_filter_preserves_package_with_a_still_kept_parent(repo_root, hashes):
    """A package pulled in by *both* an excluded and a kept parent must survive."""
    hash_line = "    --hash=sha256:deadbeef\n" if hashes else ""
    lock = (
        "# preamble\n"
        "hidapi==0.14.0 \\\n"
        "    --hash=sha256:aaaa\n"
        "    # via -r host/python/requirements.txt\n"
        "shared-dep==1.0.0 \\\n"
        f"{hash_line}"
        "    # via hidapi\n"
        "    # via pytest\n"
        "pytest==8.3.5 \\\n"
        "    --hash=sha256:bbbb\n"
        "    # via -r host/python/requirements-dev.txt\n"
    )

    filtered = _run_filter(repo_root, lock, "hidapi")

    assert "hidapi==" not in filtered
    assert "shared-dep==1.0.0" in filtered  # kept: pytest still needs it
    assert "pytest==8.3.5" in filtered
    assert "# via hidapi" not in filtered
    assert "# via pytest" in filtered
def test_filter_drops_only_transitively_hidapi_only_packages(repo_root):
    """A package solely required by the excluded package must be dropped too."""
    lock = (
        "# preamble\n"
        "hidapi==0.14.0 \\\n"
        "    --hash=sha256:aaaa\n"
        "    # via -r host/python/requirements.txt\n"
        "setuptools==84.0.0 \\\n"
        "    --hash=sha256:bbbb\n"
        "    # via hidapi\n"
        "pytest==8.3.5 \\\n"
        "    --hash=sha256:cccc\n"
        "    # via -r host/python/requirements-dev.txt\n"
    )

    filtered = _run_filter(repo_root, lock, "hidapi")

    assert "hidapi==" not in filtered
    assert "setuptools==" not in filtered
    assert "pytest==8.3.5" in filtered
