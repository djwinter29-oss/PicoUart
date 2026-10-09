from __future__ import annotations

import importlib
import subprocess
import sys
from types import SimpleNamespace

import pytest


def _load_performance():
    module = importlib.import_module("hil_test_suite.workflows.performance")
    return importlib.reload(module)


def test_parses_pass_and_fail_lines() -> None:
    performance = _load_performance()
    output = "PASS cdc0-to-cdc2: 100 bytes, 20.0 B/s\nFAIL cdc5-loopback: received data did not match\n"

    assert performance.parse_benchmark_output(output) == {
        "cdc0-to-cdc2": ("PASS", "100", "20.0"),
        "cdc5-loopback": ("FAIL", "-", "received data did not match"),
    }


def test_builds_stress_command_for_all_fixture_endpoints() -> None:
    performance = _load_performance()
    command_arguments = [item for channel in range(6) for item in (f"--cdc{channel}", f"cdc{channel}")]
    arguments = performance.parse_arguments(command_arguments)

    command = performance.build_command(arguments)

    assert command[:3] == [sys.executable, "-m", "hil_test_suite.serial.stress"]
    expected_rates = "115200,128000,153600,230400,256000,460800,921600,1000000,2000000,3000000"
    assert arguments.rates == expected_rates
    assert command[command.index("--rates") + 1] == expected_rates
    for channel in range(6):
        assert getattr(arguments, f"cdc{channel}") == f"cdc{channel}"
        assert command[command.index(f"--cdc{channel}") + 1] == f"cdc{channel}"


def test_preserves_custom_performance_rates() -> None:
    performance = _load_performance()
    endpoints = [item for channel in range(6) for item in (f"--cdc{channel}", f"cdc{channel}")]
    arguments = performance.parse_arguments(endpoints + ["--rates", "115200,256000"])

    command = performance.build_command(arguments)

    assert command[command.index("--rates") + 1] == "115200,256000"


def test_builds_incremental_stress_command() -> None:
    performance = _load_performance()
    endpoints = [item for channel in range(6) for item in (f"--cdc{channel}", f"cdc{channel}")]
    arguments = performance.parse_arguments(endpoints + ["--incremental", "--incremental-max-rate", "900000"])

    command = performance.build_command(arguments)

    assert "--incremental" in command
    assert "--rates" not in command
    assert command[command.index("--incremental-max-rate") + 1] == "900000"


def test_preserves_results_for_repeated_rates() -> None:
    performance = _load_performance()
    output = (
        "Benchmarking all six HIL fixture streams at 115200 baud\n"
        "PASS cdc5-loopback: 100 bytes, 20.0 B/s\n"
        "Benchmarking all six HIL fixture streams at 1000000 baud\n"
        "FAIL cdc5-loopback: timeout\n"
    )

    assert performance.parse_benchmark_output_by_rate(output) == {
        (115200, "cdc5-loopback"): ("PASS", "100", "20.0"),
        (1000000, "cdc5-loopback"): ("FAIL", "-", "timeout"),
    }


def test_performance_report_includes_runner_git_metadata_and_summary_table() -> None:
    performance = _load_performance()
    arguments = SimpleNamespace(
        board="pico2",
        firmware_version="1.2.3",
        firmware_commit="firmware-commit",
        runner_git_commit="runner-commit",
        runner_worktree="dirty",
        rates="115200",
        duration=10,
        payload_bytes=1024,
        artifact_path="firmware.elf",
        artifact_sha256="deadbeef",
    )
    output = "Benchmarking all six HIL fixture streams at 115200 baud\nPASS cdc0-to-cdc2: 100 bytes, 20.0 B/s\n"

    report = performance.format_result_entry(arguments, "2026-10-08T00:00:00+00:00", 0, output)

    assert "**Runner Git commit:** `runner-commit`" in report
    assert "**Runner worktree:** `dirty`" in report
    assert "| 115200 | cdc0-to-cdc2 | PASS | 100 | 20.0 |" in report
    assert "Command:" not in report


def test_incremental_report_shows_only_attempted_rates_and_highest_supported() -> None:
    performance = _load_performance()
    arguments = SimpleNamespace(
        board="pico2",
        firmware_version="1.2.3",
        firmware_commit="firmware-commit",
        runner_git_commit="runner-commit",
        runner_worktree="clean",
        rates="115200",
        incremental=True,
        duration=30,
        payload_bytes=1024,
        artifact_path="firmware.elf",
        artifact_sha256="deadbeef",
    )
    streams = ["cdc0-to-cdc2", "cdc2-to-cdc0", "cdc3-to-cdc4", "cdc4-to-cdc3", "cdc1-loopback", "cdc5-loopback"]
    output_lines = []
    for rate, status in ((460800, "PASS"), (560800, "PASS"), (660800, "FAIL")):
        output_lines.append(f"Incremental rate: {rate} baud")
        output_lines.append(f"Benchmarking all six HIL fixture streams at {rate} baud")
        output_lines.extend(
            f"{status} {label}: 1024 bytes, 100.0 B/s" if status == "PASS" else f"FAIL {label}: timeout"
            for label in streams
        )
    output = "\n".join(output_lines)

    report = performance.format_result_entry(arguments, "2026-10-08T00:00:00+00:00", 0, output)

    assert "460800 | cdc0-to-cdc2 | PASS" in report
    assert "560800 | cdc0-to-cdc2 | PASS" in report
    assert "660800 | cdc0-to-cdc2 | FAIL" in report
    assert "Highest supported concurrent rate:** `560800`" in report
    assert "First failed tested rate:** `660800`" in report
    assert "760800" not in report


@pytest.mark.parametrize(
    ("command_status", "health_error", "expected"),
    [(0, None, 0), (0, "device lost", 1), (1, None, 1)],
)
def test_main_combines_benchmark_and_health_status(monkeypatch, command_status, health_error, expected) -> None:
    performance = _load_performance()
    arguments = performance.parse_arguments(
        [item for channel in range(6) for item in (f"--cdc{channel}", f"cdc{channel}")]
    )
    arguments.board = "pico"
    arguments.firmware_version = "1.2.3"
    arguments.firmware_commit = "abc1234"
    arguments.artifact = None
    arguments.no_record = True
    clean = {
        "channels": {index: 1 for index in range(6)},
        "overruns": {index: 0 for index in range(6)},
        "firmware_version": "1.2.3",
        "error": None,
    }
    after = {**clean, "error": health_error}
    snapshots = iter([clean, after])
    monkeypatch.setattr(performance, "artifact_metadata", lambda _artifact: {"path": "none", "sha256": "none"})
    monkeypatch.setattr(performance, "git_metadata", lambda: ("commit", "clean"))
    monkeypatch.setattr(performance, "build_command", lambda _arguments: ["stress"])
    monkeypatch.setattr(performance, "collect_hid_health", lambda: next(snapshots))
    monkeypatch.setattr(performance, "health_evidence", lambda _snapshot: "")
    monkeypatch.setattr(
        performance.subprocess,
        "run",
        lambda *_args, **_kwargs: SimpleNamespace(returncode=command_status, stdout=""),
    )

    assert performance.main(arguments) == expected