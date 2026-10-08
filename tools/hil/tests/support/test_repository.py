from __future__ import annotations

import importlib
import subprocess


def _load_repository():
    module = importlib.import_module("hil_test_suite.support.repository")
    return importlib.reload(module)


def test_git_metadata_reports_commit_and_dirty_worktree(monkeypatch) -> None:
    repository = _load_repository()
    responses = iter(
        [
            subprocess.CompletedProcess([], 0, "0123456789abcdef\n", ""),
            subprocess.CompletedProcess([], 0, " M source.py\n", ""),
        ]
    )
    commands = []

    def run(command, **_kwargs):
        commands.append(command)
        return next(responses)

    monkeypatch.setattr(repository.subprocess, "run", run)

    assert repository.git_metadata() == ("0123456789abcdef", "dirty")
    assert commands == [["git", "rev-parse", "HEAD"], ["git", "status", "--porcelain"]]