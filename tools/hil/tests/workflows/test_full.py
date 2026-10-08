from __future__ import annotations

import importlib
import sys
from datetime import datetime, timezone
from pathlib import Path
from types import SimpleNamespace

import pytest


def _load_full():
    module = importlib.import_module("hil_test_suite.workflows.full")
    return importlib.reload(module)


def _arguments() -> SimpleNamespace:
    return SimpleNamespace(
        pico_cdc0="cdc0",
        pico_cdc1="cdc1",
        pico_cdc2="cdc2",
        pico_cdc3="cdc3",
        pico_cdc4="cdc4",
        pico_cdc5="cdc5",
        functional_baud=115200,
        functional_payload_bytes=64,
        performance_payload_bytes=1024,
        timeout=3.0,
        duration=10.0,
        rates="115200",
        board="pico",
        tester="test",
        firmware_version="1.2.3",
        firmware_commit="abc1234",
        artifact=None,
        skip_functional=False,
        skip_performance=False,
        continue_after_functional_failure=False,
        no_record=True,
    )


def test_streams_child_output_and_status() -> None:
    full = _load_full()
    command = [sys.executable, "-c", "print('child-output', flush=True)"]

    status, transcript = full.run_child("stream check", command)

    assert status == 0
    assert "Command:" in transcript
    assert "child-output" in transcript


def test_builds_internal_workflow_commands() -> None:
    full = _load_full()
    arguments = _arguments()

    functional = full.build_functional_command(arguments)
    performance = full.build_performance_command(arguments)

    assert functional[:3] == [sys.executable, "-m", "hil_test_suite.workflows.functional"]
    assert performance[:3] == [sys.executable, "-m", "hil_test_suite.workflows.performance"]


def test_builds_incremental_performance_command() -> None:
    full = _load_full()
    arguments = _arguments()
    arguments.incremental_performance = True
    arguments.incremental_start_rate = 460800
    arguments.incremental_rate_step = 100000
    arguments.incremental_max_rate = 800000

    command = full.build_performance_command(arguments)

    assert "--incremental" in command
    assert "--rates" not in command
    assert command[command.index("--incremental-max-rate") + 1] == "800000"


def test_parse_defaults_to_usb_sustainable_rate() -> None:
    full = _load_full()
    endpoints = [item for channel in range(6) for item in (f"--pico-cdc{channel}", f"cdc{channel}")]

    arguments = full.parse_arguments(endpoints)

    assert arguments.rates == "115200"
    assert arguments.record_dir == full.DEFAULT_RECORDS_DIR


def test_git_metadata_reads_commit_and_worktree_state(monkeypatch) -> None:
    full = _load_full()
    responses = iter([SimpleNamespace(stdout="0123456789abcdef\n"), SimpleNamespace(stdout=" M file.py\n")])
    commands = []

    def run(command, **_kwargs):
        commands.append(command)
        return next(responses)

    monkeypatch.setattr(full.subprocess, "run", run)

    assert full.git_metadata() == ("0123456789abcdef", "dirty")
    assert commands == [["git", "rev-parse", "HEAD"], ["git", "status", "--porcelain"]]


def test_parser_does_not_advertise_unused_uart0_baud_option() -> None:
    full = _load_full()

    assert "--uart0-baud" not in full.build_parser().format_help()


def test_marks_failed_functional_phase_as_fail() -> None:
    full = _load_full()
    arguments = SimpleNamespace(
        board="pico",
        tester="test",
        firmware_version="1.2.3",
        firmware_commit="abc1234",
    )

    entry = full.format_result_entry(arguments, "2026-09-20T00:00:00+00:00", (1, "functional failed"), None)

    assert "**Overall result:** `FAIL`" in entry


def test_writes_fixed_record_sections_and_board_metadata(tmp_path: Path) -> None:
    full = _load_full()
    arguments = SimpleNamespace(
        board="pico2",
        tester="operator",
        firmware_version="1.2.3",
        firmware_commit="abc1234",
        runner_git_commit="0123456789abcdef",
        runner_worktree="dirty",
        rates="460800",
        full_fixture=True,
        artifact_path="pico_uart.elf",
        artifact_sha256="deadbeef",
    )
    run_at = datetime(2026, 10, 8, 14, 30, tzinfo=timezone.utc)
    functional_output = "\n".join(
        [
            "HID health summary: health clean before",
            "RUN HW UART0 to PIO UART2: child command hidden",
            "PASS pico-to-peer: 117 bytes",
            "PASS peer-to-pico: 117 bytes",
            "RUN PIO UART3 to PIO UART4: child command hidden",
            "PASS pico-to-peer: 117 bytes",
            "PASS peer-to-pico: 117 bytes",
            "RUN HW UART1 loopback: child command hidden",
            "PASS pico-loopback: 118 bytes",
            "RUN PIO UART5 loopback: child command hidden",
            "PASS pico-loopback: 118 bytes",
            "HID health summary: health clean after",
        ]
    )
    performance_output = "\n".join(
        [
            "Benchmarking all six HIL fixture streams at 460800 baud",
            "PASS cdc0-to-cdc2: 1000 bytes, 100.0 B/s",
            "PASS cdc2-to-cdc0: 1000 bytes, 101.0 B/s",
            "PASS cdc3-to-cdc4: 1000 bytes, 102.0 B/s",
            "PASS cdc4-to-cdc3: 1000 bytes, 103.0 B/s",
            "PASS cdc1-loopback: 1000 bytes, 104.0 B/s",
            "PASS cdc5-loopback: 1000 bytes, 105.0 B/s",
            "HID health summary: health clean final",
        ]
    )
    content = full.format_result_entry(
        arguments, run_at.isoformat(), (0, functional_output), (0, performance_output)
    )

    record = full.write_hil_record(tmp_path, run_at, arguments.board, content)

    assert record.name == "2026-10-08-143000Z-pico2-hil.md"
    text = record.read_text(encoding="utf-8")
    assert "**Overall result:** `PASS`" in text
    assert "**Artifact SHA-256:** `deadbeef`" in text
    assert "**Runner Git commit:** `0123456789abcdef`" in text
    assert "**Runner worktree:** `dirty`" in text
    assert "## Functional Summary" in text
    assert "| HW UART0 to PIO UART2 | CDC0 <-> CDC2 | PASS | 234 |" in text
    assert "## Concurrent Performance Summary" in text
    assert "| 460800 | cdc0-to-cdc2 | PASS | 1000 | 100.0 B/s |" in text
    assert "health clean final" in text
    assert "child command hidden" not in text
    assert "Command:" not in text


def test_incremental_record_shows_ceiling_and_only_attempted_rates() -> None:
    full = _load_full()
    arguments = _arguments()
    arguments.incremental_performance = True
    arguments.full_fixture = True
    functional_output = "\n".join(
        [
            f"RUN {label}: child command hidden\nPASS pico-to-peer: 117 bytes\nPASS peer-to-pico: 117 bytes"
            for label in ("HW UART0 to PIO UART2", "PIO UART3 to PIO UART4")
        ]
        + [
            "RUN HW UART1 loopback: child command hidden\nPASS pico-loopback: 118 bytes",
            "RUN PIO UART5 loopback: child command hidden\nPASS pico-loopback: 118 bytes",
        ]
    )
    stream_labels = full.PERFORMANCE_LINKS
    performance_lines = []
    for rate, outcome in ((460800, "PASS"), (560800, "PASS"), (660800, "FAIL")):
        performance_lines.append(f"Incremental rate: {rate} baud")
        performance_lines.append(f"Benchmarking all six HIL fixture streams at {rate} baud")
        performance_lines.extend(
            f"{outcome} {label}: 1024 bytes, 100.0 B/s" if outcome == "PASS" else f"FAIL {label}: timeout"
            for label in stream_labels
        )

    report = full.format_result_entry(
        arguments,
        "2026-10-08T00:00:00+00:00",
        (0, functional_output),
        (0, "\n".join(performance_lines)),
    )

    assert "**Overall result:** `PASS`" in report
    assert "Highest supported concurrent rate:** `560800 baud`" in report
    assert "First failed tested rate:** `660800 baud`" in report
    assert "760800" not in report
    assert "child command hidden" not in report


def test_record_writer_never_overwrites_same_run_name(tmp_path: Path) -> None:
    full = _load_full()
    run_at = datetime(2026, 10, 8, 14, 30, tzinfo=timezone.utc)

    first = full.write_hil_record(tmp_path, run_at, "Pico 2", "first record")
    second = full.write_hil_record(tmp_path, run_at, "Pico 2", "second record")

    assert first.name == "2026-10-08-143000Z-pico-2-hil.md"
    assert second.name == "2026-10-08-143000Z-pico-2-hil-02.md"
    assert first.read_text(encoding="utf-8") == "first record\n"
    assert second.read_text(encoding="utf-8") == "second record\n"


@pytest.mark.parametrize(
    ("functional_code", "continue_after_failure", "expected_phases", "expected_status"),
    [
        (0, False, ["functional test", "performance test"], 0),
        (1, False, ["functional test"], 1),
        (1, True, ["functional test", "performance test"], 1),
    ],
)
def test_main_sequences_phases_and_preserves_failure(
    monkeypatch, functional_code, continue_after_failure, expected_phases, expected_status
) -> None:
    full = _load_full()
    arguments = _arguments()
    arguments.continue_after_functional_failure = continue_after_failure
    phases = []
    monkeypatch.setattr(full, "git_metadata", lambda: ("commit", "clean"))
    monkeypatch.setattr(full, "artifact_metadata", lambda _artifact: {"path": "none", "sha256": "none"})

    def run_child(label, _command):
        phases.append(label)
        return (functional_code if label == "functional test" else 0, label)

    monkeypatch.setattr(full, "run_child", run_child)

    assert full.main(arguments) == expected_status
    assert phases == expected_phases


def test_main_rejects_skipping_both_phases(monkeypatch, capsys) -> None:
    full = _load_full()
    arguments = _arguments()
    arguments.skip_functional = True
    arguments.skip_performance = True
    monkeypatch.setattr(full, "git_metadata", lambda: ("commit", "clean"))
    monkeypatch.setattr(full, "artifact_metadata", lambda _artifact: {"path": "none", "sha256": "none"})
    monkeypatch.setattr(full, "run_child", lambda *_args: pytest.fail("no phase should run"))

    assert full.main(arguments) == 2
    assert "at least one test phase must run" in capsys.readouterr().err