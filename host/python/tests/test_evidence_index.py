# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Lock the Pico 2 evidence gap and require indexed HIL records."""

import re
from pathlib import Path


def test_record_indexes_link_every_record_and_keep_the_pico2_gap(repo_root: Path):
    records = repo_root / "docs" / "tests" / "records"
    main_index = (records / "README.md").read_text(encoding="utf-8")
    index = " ".join(main_index.split())
    assert "Pico 2 rows below stop on 2026-10-10." in index
    assert "No file in this directory is an exact-artifact Pico 2 run for the current release." in index
    assert "A Pico `PASS` does not qualify `pico2`." in index
    assert "archive/README.md" in main_index
    for directory, page in (
        (records, records / "README.md"),
        (records / "archive", records / "archive" / "README.md"),
    ):
        raw = page.read_text(encoding="utf-8")
        names = sorted(path.name for path in directory.glob("*.md") if path != page)
        assert names
        missing = [name for name in names if f"]({name})" not in raw]
        assert missing == []
        dangling = [
            name for name in re.findall(r"\]\(([^)]+\.md)\)", raw) if not (page.parent / name).is_file()
        ]
        assert dangling == []
