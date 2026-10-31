# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Lock the Pico 2 evidence gap and require every record file to be linked."""

from pathlib import Path
import re


def test_evidence_index_links_every_record_and_keeps_the_pico2_gap(repo_root: Path):
    records = repo_root / "docs" / "tests" / "records"
    raw = (records / "README.md").read_text(encoding="utf-8")
    index = " ".join(raw.split())
    assert "Pico 2 rows below stop on 2026-10-10 and predate the v0.8.6 Pico release line." in index
    assert "No file in this directory is an exact-artifact Pico 2 run of that line." in index
    assert "A Pico `PASS` does not qualify `pico2`." in index
    assert "Every Markdown file in this directory is linked from this page." in index
    names = sorted(path.name for path in records.glob("*.md") if path.name != "README.md")
    assert names
    missing = [name for name in names if f"]({name})" not in raw]
    assert missing == []
    dangling = [
        name for name in re.findall(r"\]\(([^)]+\.md)\)", raw) if not (records / name).is_file()
    ]
    assert dangling == []
