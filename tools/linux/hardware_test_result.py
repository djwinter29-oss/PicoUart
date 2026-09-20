#!/usr/bin/env python3
"""Shared result-log helpers for the physical UART test runners."""

from pathlib import Path


def prepend_result(results_file: Path, entry: str) -> None:
    """Insert one newest-first result entry before the reusable template."""
    if not results_file.is_file():
        raise FileNotFoundError(f"results file not found: {results_file}")

    document = results_file.read_text(encoding="utf-8")
    template_marker = "\n## Template"
    template_point = document.find(template_marker)
    if template_point < 0:
        raise ValueError(f"results file has no template marker: {results_file}")

    first_result = document.find("\n## ", document.find("# Performance Test Results") + 1)
    insertion_point = first_result if 0 <= first_result < template_point else template_point

    updated = document[:insertion_point].rstrip() + "\n\n" + entry.strip() + "\n" + document[insertion_point:]
    results_file.write_text(updated, encoding="utf-8")
