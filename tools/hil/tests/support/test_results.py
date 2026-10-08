from __future__ import annotations

import hashlib
import importlib

import pytest


def _load_results():
    module = importlib.import_module("hil_test_suite.support.results")
    return importlib.reload(module)


def test_prepends_new_result_before_template(tmp_path) -> None:
    results = _load_results()
    results_file = tmp_path / "results.md"
    results_file.write_text("# Results\n\n## Template\n", encoding="utf-8")

    results.prepend_result(results_file, "## New result\n\n**Result:** `PASS`")

    assert results_file.read_text(encoding="utf-8") == (
        "# Results\n\n## New result\n\n**Result:** `PASS`\n\n## Template\n"
    )


def test_creates_missing_local_log(tmp_path) -> None:
    results = _load_results()
    results_file = tmp_path / "build" / "hil-results.md"

    results.prepend_result(results_file, "## New result\n\n**Result:** `PASS`")

    assert results_file.read_text(encoding="utf-8") == (
        "# PicoUart HIL Results\n\n## New result\n\n**Result:** `PASS`\n\n## Template\n"
    )


def test_rejects_existing_log_without_template(tmp_path) -> None:
    results = _load_results()
    results_file = tmp_path / "results.md"
    results_file.write_text("# Results\n", encoding="utf-8")

    with pytest.raises(ValueError, match="template marker"):
        results.prepend_result(results_file, "## New result")


def test_artifact_metadata_for_missing_artifact() -> None:
    results = _load_results()

    assert results.artifact_metadata(None) == {"path": "not supplied", "sha256": "not supplied"}


def test_artifact_metadata_returns_path_and_sha256(tmp_path) -> None:
    results = _load_results()
    artifact = tmp_path / "firmware.uf2"
    content = b"firmware artifact bytes"
    artifact.write_bytes(content)

    metadata = results.artifact_metadata(artifact)

    assert metadata["path"] == str(artifact.resolve())
    assert metadata["sha256"] == hashlib.sha256(content).hexdigest()


def test_artifact_metadata_rejects_missing_file(tmp_path) -> None:
    results = _load_results()

    with pytest.raises(FileNotFoundError, match="artifact not found"):
        results.artifact_metadata(tmp_path / "missing.uf2")