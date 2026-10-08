"""Git revision metadata for HIL evidence."""

from __future__ import annotations

import subprocess

from .paths import REPO_ROOT


def git_metadata() -> tuple[str, str]:
    """Return the checkout commit and whether the worktree has local changes."""
    try:
        commit = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, capture_output=True, text=True, check=True
        ).stdout.strip()
        status = subprocess.run(
            ["git", "status", "--porcelain"], cwd=REPO_ROOT, capture_output=True, text=True, check=True
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return "unknown", "unknown"
    return commit, "dirty" if status.strip() else "clean"