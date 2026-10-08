from __future__ import annotations

import importlib
import sys
import subprocess
from types import SimpleNamespace

import pytest


def _load_functional():
    module = importlib.import_module("hil_test_suite.workflows.functional")
    return importlib.reload(module)


def _arguments() -> SimpleNamespace:
    return SimpleNamespace(
        pico_cdc0="cdc0",
        pico_cdc1="cdc1",
        pico_cdc2="cdc2",
        pico_cdc3="cdc3",
        pico_cdc4="cdc4",
        pico_cdc5="cdc5",
        baud=115200,
        payload_bytes=64,
        timeout=3.0,
        stage="all",
    )


def test_builds_all_fixture_stage_commands() -> None:
    functional = _load_functional()

    commands = functional.build_stage_commands(_arguments())

    assert [label for label, _ in commands] == [
        "HW UART0 to PIO UART2",
        "PIO UART3 to PIO UART4",
        "HW UART1 loopback",
        "PIO UART5 loopback",
    ]
    assert commands[0][1][:3] == [sys.executable, "-m", "hil_test_suite.serial.bridge"]
    assert "--loopback" in commands[-1][1]
    assert "--peer-port" in commands[1][1]


def test_selects_one_fixture_stage() -> None:
    functional = _load_functional()
    arguments = _arguments()
    arguments.stage = "2"

    commands = functional.build_stage_commands(arguments)

    assert [label for label, _ in commands] == ["PIO UART3 to PIO UART4"]


def test_rejects_duplicate_fixture_endpoints(capsys) -> None:
    functional = _load_functional()
    arguments = _arguments()
    arguments.pico_cdc2 = arguments.pico_cdc0

    assert not functional.fixture_paths_valid(arguments)
    assert "same endpoint" in capsys.readouterr().err


def test_single_successful_stage_is_recorded_partial() -> None:
    functional = _load_functional()
    arguments = SimpleNamespace(
        board="pico",
        firmware_version="0.0.0",
        firmware_commit="local",
        runner_git_commit="0123456789abcdef",
        runner_worktree="clean",
        baud=115200,
        payload_bytes=64,
    )
    clean = {
        "channels": {index: 1 for index in range(6)},
        "overruns": {index: 0 for index in range(6)},
        "error": None,
    }

    entry = functional.format_result_entry(
        arguments,
        "2026-09-20T00:00:00+00:00",
        [("HW UART0 to PIO UART2", 0, "PASS pico-to-peer: 117 bytes\nPASS peer-to-pico: 117 bytes")],
        clean,
        clean,
    )

    assert "**Result:** `PARTIAL`" in entry
    assert "**Runner Git commit:** `0123456789abcdef`" in entry
    assert "| HW UART0 to PIO UART2 | PASS | 234 |" in entry


@pytest.mark.parametrize(("stage_status", "expected"), [(0, 0), (1, 1)])
def test_main_returns_stage_status_without_hardware(monkeypatch, stage_status, expected) -> None:
    functional = _load_functional()
    arguments = _arguments()
    arguments.board = "pico"
    arguments.firmware_version = "1.2.3"
    arguments.firmware_commit = "abc1234"
    arguments.artifact = None
    arguments.continue_on_failure = False
    arguments.no_record = True
    snapshot = {
        "channels": {index: 1 for index in range(6)},
        "overruns": {index: 0 for index in range(6)},
        "firmware_version": "1.2.3",
        "error": None,
    }
    monkeypatch.setattr(functional, "artifact_metadata", lambda _artifact: {"path": "none", "sha256": "none"})
    monkeypatch.setattr(functional, "git_metadata", lambda: ("commit", "clean"))
    monkeypatch.setattr(functional, "fixture_paths_valid", lambda _arguments: True)
    monkeypatch.setattr(functional, "collect_hid_health", lambda: snapshot)
    monkeypatch.setattr(functional, "health_evidence", lambda _snapshot: "")
    monkeypatch.setattr(functional, "build_stage_commands", lambda _arguments: [("stage", ["child"])])
    monkeypatch.setattr(
        functional.subprocess,
        "run",
        lambda *_args, **_kwargs: SimpleNamespace(returncode=stage_status, stdout="stage output\n"),
    )

    assert functional.main(arguments) == expected