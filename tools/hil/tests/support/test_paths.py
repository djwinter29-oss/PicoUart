from __future__ import annotations

import importlib

import pytest


def _load_paths():
    module = importlib.import_module("hil_test_suite.support.paths")
    return importlib.reload(module)


def test_finds_checkout_root(repo_root) -> None:
    paths = _load_paths()

    assert paths.find_repo_root() == repo_root


def test_rejects_directory_outside_checkout(tmp_path, monkeypatch) -> None:
    paths = _load_paths()
    monkeypatch.setattr(paths, "PACKAGE_DIR", tmp_path)

    with pytest.raises(RuntimeError, match="source checkout"):
        paths.find_repo_root()