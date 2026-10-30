# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Repository paths shared by the checkout-bound HIL runners."""

from pathlib import Path

PACKAGE_DIR = Path(__file__).resolve().parent


def find_repo_root() -> Path:
    """Find the PicoUart checkout containing this editable/local test suite."""
    for parent in PACKAGE_DIR.parents:
        if (parent / "firmware").is_dir() and (parent / "tools" / "hil").is_dir():
            return parent
    raise RuntimeError("PicoUart HIL tools must run from a PicoUart source checkout")


REPO_ROOT = find_repo_root()
