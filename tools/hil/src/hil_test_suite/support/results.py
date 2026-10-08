#!/usr/bin/env python3
"""Shared result-log helpers for the physical UART test runners."""

from pathlib import Path
import hashlib


def prepend_result(results_file: Path, entry: str) -> None:
    """Insert one newest-first result entry into a local HIL log."""
    if results_file.is_file():
        document = results_file.read_text(encoding="utf-8")
    else:
        results_file.parent.mkdir(parents=True, exist_ok=True)
        document = "# PicoUart HIL Results\n\n## Template\n"
    template_marker = "\n## Template"
    template_point = document.find(template_marker)
    if template_point < 0:
        raise ValueError(f"results file has no template marker: {results_file}")

    header_end = document.find("\n\n")
    search_from = header_end + 2 if header_end >= 0 else 0
    first_result = document.find("\n## ", search_from)
    insertion_point = first_result if 0 <= first_result < template_point else template_point

    updated = document[:insertion_point].rstrip() + "\n\n" + entry.strip() + "\n" + document[insertion_point:]
    results_file.write_text(updated, encoding="utf-8")


def artifact_metadata(artifact: Path | None) -> dict[str, str]:
    """Return the selected firmware artifact and its SHA-256 digest."""
    if artifact is None:
        return {"path": "not supplied", "sha256": "not supplied"}
    artifact = artifact.resolve()
    if not artifact.is_file():
        raise FileNotFoundError(f"artifact not found: {artifact}")
    digest = hashlib.sha256()
    with artifact.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"path": str(artifact), "sha256": digest.hexdigest()}
