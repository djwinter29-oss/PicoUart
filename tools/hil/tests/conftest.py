# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
from __future__ import annotations

from pathlib import Path

import pytest


@pytest.fixture
def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]
