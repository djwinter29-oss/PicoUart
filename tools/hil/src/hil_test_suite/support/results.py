#!/usr/bin/env python3
"""Shared result-log helpers for the physical UART test runners."""

import hashlib
import os
import tempfile
from pathlib import Path

try:
    import fcntl
except ImportError:
    import msvcrt

    fcntl = None


def firmware_version_for_report(version: str | None) -> str | None:
    """Hide development placeholder versions while retaining release versions."""
    if version is None:
        return None
    normalized = version.strip()
    if normalized in {"0.0.0", "0.0.0-dev"}:
        return None
    return normalized


def prepend_result(results_file: Path, entry: str) -> None:
    """Insert one newest-first result entry, serializing concurrent writers."""
    results_file.parent.mkdir(parents=True, exist_ok=True)
    lock_path = results_file.with_name(f".{results_file.name}.lock")
    with lock_path.open("a+b") as lock_file:
        # Keep a stable sidecar inode so writers serialize across result-file replacements.
        if fcntl is None:
            lock_file.seek(0)
            msvcrt.locking(lock_file.fileno(), msvcrt.LK_LOCK, 1)
        else:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
        if results_file.is_file():
            document = results_file.read_text(encoding="utf-8")
        else:
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
        temporary_path = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", dir=results_file.parent, prefix=f".{results_file.name}.", delete=False
            ) as temporary_file:
                temporary_path = Path(temporary_file.name)
                temporary_file.write(updated)
                temporary_file.flush()
                os.fsync(temporary_file.fileno())
            os.replace(temporary_path, results_file)
        finally:
            if temporary_path is not None:
                temporary_path.unlink(missing_ok=True)


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
