"""Dated hardware summaries stay indexed and preserve qualification caveats."""


def test_dated_validation_summary_is_indexed_and_marks_overrun_caveat(repo_root):
    index = (repo_root / "docs/tests/README.md").read_text(encoding="utf-8")
    document = (repo_root / "docs/tests/hardware-validation-2026-10-01.md").read_text(
        encoding="utf-8"
    )

    assert "hardware-validation-2026-10-01.md" in index
    assert "`PARTIAL" in document
    assert "cdc5=10984" in document
    assert "responsible phase is therefore unknown" in document
    assert "raw" not in document.lower()
