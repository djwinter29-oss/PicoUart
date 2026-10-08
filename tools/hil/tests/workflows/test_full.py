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


def test_parse_defaults_to_usb_sustainable_rate() -> None:
    full = _load_full()
    endpoints = [item for channel in range(6) for item in (f"--pico-cdc{channel}", f"cdc{channel}")]

    arguments = full.parse_arguments(endpoints)

    assert arguments.rates == "115200"
    assert arguments.record_dir == full.DEFAULT_RECORDS_DIR


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

    assert "**Result:** `FAIL`" in entry


def test_writes_fixed_record_sections_and_board_metadata(tmp_path: Path) -> None:
    full = _load_full()
    arguments = SimpleNamespace(
        board="pico2",
        tester="operator",
        firmware_version="1.2.3",
        firmware_commit="abc1234",
        full_fixture=True,
        artifact_path="pico_uart.elf",
        artifact_sha256="deadbeef",
    )
    run_at = datetime(2026, 10, 8, 14, 30, tzinfo=timezone.utc)
    content = full.format_result_entry(
        arguments, run_at.isoformat(), (0, "functional output"), (0, "performance output")
    )

    record = full.write_hil_record(tmp_path, run_at, arguments.board, content)

    assert record.name == "2026-10-08-143000Z-pico2-hil.md"
    text = record.read_text(encoding="utf-8")
    assert "**Result:** `PASS`" in text
    assert "**Artifact SHA-256:** `deadbeef`" in text
    assert "## Functional Test" in text
    assert "## Performance Test" in text
    assert "functional output" in text
    assert "performance output" in text


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
    monkeypatch.setattr(full, "artifact_metadata", lambda _artifact: {"path": "none", "sha256": "none"})
    monkeypatch.setattr(full, "run_child", lambda *_args: pytest.fail("no phase should run"))

    assert full.main(arguments) == 2
    assert "at least one test phase must run" in capsys.readouterr().err