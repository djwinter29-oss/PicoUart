# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Lock the Pico 2 evidence gap without treating every development note as qualification."""

from pathlib import Path
import re


def test_evidence_index_links_pico2_records_and_keeps_the_gap(repo_root: Path):
    records = repo_root / "docs" / "tests" / "records"
    raw = (records / "README.md").read_text(encoding="utf-8")
    index = " ".join(raw.split())
    assert "Pico 2 rows below stop on 2026-10-10 and predate the v0.8.6 Pico release line." in index
    assert "No file in this directory is an exact-artifact Pico 2 run of that line." in index
    assert "A Pico `PASS` does not qualify `pico2`." in index
    assert "Every Pico 2 record in this directory is listed below." in index
    pico2 = sorted(path.name for path in records.glob("*pico2*.md"))
    assert pico2
    missing = [name for name in pico2 if f"]({name})" not in raw]
    assert missing == []
    dangling = [
        name for name in re.findall(r"\]\(([^)]+\.md)\)", raw) if not (records / name).is_file()
    ]
    assert dangling == []
