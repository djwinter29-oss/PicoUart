#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

TOOLS = Path(__file__).resolve().parents[2] / "tools" / "hardware"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))


def _load(name: str):
    path = TOOLS / f"{name}.py"
    if not path.exists():
        path = TOOLS / "hardware" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def functional_arguments() -> SimpleNamespace:
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


def performance_arguments() -> SimpleNamespace:
    return SimpleNamespace(
        cdc0="cdc0",
        cdc1="cdc1",
        cdc2="cdc2",
        cdc3="cdc3",
        cdc4="cdc4",
        cdc5="cdc5",
        rates="115200,460800",
        duration=10.0,
        payload_bytes=1024,
        timeout=3.0,
    )


def test_functional_runner_builds_all_documented_stages() -> None:
    runner = _load("run_functional_test")
    commands = runner.build_stage_commands(functional_arguments())

    assert [label for label, _ in commands] == [
        "HW UART0 to PIO UART2",
        "PIO UART3 to PIO UART4",
        "HW UART1 loopback",
        "PIO UART5 loopback",
    ]
    assert "--loopback" in commands[-1][1]
    assert "--peer-port" in commands[1][1]


def test_functional_runner_selects_one_stage() -> None:
    runner = _load("run_functional_test")
    arguments = functional_arguments()
    arguments.stage = "2"

    commands = runner.build_stage_commands(arguments)

    assert [label for label, _ in commands] == ["PIO UART3 to PIO UART4"]


def test_hardware_runner_streams_child_output_and_status() -> None:
    runner = _load("run_hardware_test")
    command = [sys.executable, "-c", "print('child-output', flush=True)"]

    status, transcript = runner.run_child("stream check", command)

    assert status == 0
    assert "Command:" in transcript
    assert "child-output" in transcript


def test_hid_health_module_resolves_repository_root() -> None:
    health = _load("hardware_test_health")

    assert health.REPO_ROOT == Path(__file__).resolve().parents[2]
    assert health.HOST_PYTHON_SRC == health.REPO_ROOT / "host/python/src"
    assert health.HID_MODULE == "pico_uart"


def test_single_functional_stage_is_recorded_partial() -> None:
    runner = _load("run_functional_test")
    arguments = SimpleNamespace(
        board="pico",
        firmware_version="0.0.0",
        firmware_commit="local",
        baud=115200,
        payload_bytes=64,
    )
    clean = {
        "channels": {index: 1 for index in range(6)},
        "overruns": {index: 0 for index in range(6)},
        "error": None,
    }

    entry = runner.format_result_entry(
        arguments, "2026-09-20T00:00:00+00:00", [("HW UART0 to PIO UART2", 0, "PASS")], clean, clean
    )

    assert "**Result:** `PARTIAL`" in entry


def test_performance_runner_parses_pass_and_fail_lines() -> None:
    runner = _load("run_performance_test")
    output = "PASS cdc0-to-cdc2: 100 bytes, 20.0 B/s\nFAIL cdc5-loopback: received data did not match\n"

    assert runner.parse_benchmark_output(output) == {
        "cdc0-to-cdc2": ("PASS", "100", "20.0"),
        "cdc5-loopback": ("FAIL", "-", "received data did not match"),
    }


def test_performance_runner_accepts_all_fixture_cdc_endpoints(monkeypatch) -> None:
    runner = _load("run_performance_test")
    arguments = ["run_performance_test.py"]
    for channel in range(6):
        arguments.extend([f"--cdc{channel}", f"cdc{channel}"])
    monkeypatch.setattr(sys, "argv", arguments)

    arguments = runner.parse_arguments()
    command = runner.build_command(arguments)

    for channel in range(6):
        assert getattr(arguments, f"cdc{channel}") == f"cdc{channel}"
        assert command[command.index(f"--cdc{channel}") + 1] == f"cdc{channel}"


def test_full_hardware_runner_defaults_to_usb_sustainable_rate(monkeypatch) -> None:
    runner = _load("run_hardware_test")
    arguments = [
        "run_hardware_test.py",
        "--pico-cdc0",
        "cdc0",
        "--pico-cdc1",
        "cdc1",
        "--pico-cdc2",
        "cdc2",
        "--pico-cdc3",
        "cdc3",
        "--pico-cdc4",
        "cdc4",
        "--pico-cdc5",
        "cdc5",
    ]
    monkeypatch.setattr(sys, "argv", arguments)

    assert runner.parse_arguments().rates == "115200"


def test_performance_runner_preserves_rate_results() -> None:
    runner = _load("run_performance_test")
    output = (
        "Benchmarking all six HIL fixture streams at 115200 baud\n"
        "PASS cdc5-loopback: 100 bytes, 20.0 B/s\n"
        "Benchmarking all six HIL fixture streams at 1000000 baud\n"
        "FAIL cdc5-loopback: timeout\n"
    )

    assert runner.parse_benchmark_output_by_rate(output) == {
        (115200, "cdc5-loopback"): ("PASS", "100", "20.0"),
        (1000000, "cdc5-loopback"): ("FAIL", "-", "timeout"),
    }


def test_hardware_runner_marks_failed_functional_phase_as_fail() -> None:
    runner = _load("run_hardware_test")
    arguments = SimpleNamespace(
        board="pico",
        tester="test",
        firmware_version="1.2.3",
        firmware_commit="abc1234",
    )

    entry = runner.format_result_entry(arguments, "2026-09-20T00:00:00+00:00", (1, "functional failed"), None)

    assert "**Result:** `FAIL`" in entry


def test_result_helper_prepends_before_template(tmp_path: Path) -> None:
    helper = _load("hardware_test_result")
    results = tmp_path / "results.md"
    results.write_text("# Results\n\n## Template\n", encoding="utf-8")

    helper.prepend_result(results, "## New result\n\n**Result:** `PASS`")

    assert results.read_text(encoding="utf-8") == ("# Results\n\n## New result\n\n**Result:** `PASS`\n\n## Template\n")


def test_result_helper_creates_missing_local_log(tmp_path: Path) -> None:
    helper = _load("hardware_test_result")
    results = tmp_path / "build" / "hil-results.md"

    helper.prepend_result(results, "## New result\n\n**Result:** `PASS`")

    assert results.read_text(encoding="utf-8") == (
        "# PicoUart HIL Results\n\n## New result\n\n**Result:** `PASS`\n\n## Template\n"
    )


def test_result_helper_rejects_missing_template(tmp_path: Path) -> None:
    helper = _load("hardware_test_result")
    results = tmp_path / "results.md"
    results.write_text("# Results\n", encoding="utf-8")

    with pytest.raises(ValueError, match="template marker"):
        helper.prepend_result(results, "## New result")
