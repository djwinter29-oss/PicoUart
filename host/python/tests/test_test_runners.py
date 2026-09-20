#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

TOOLS = Path(__file__).resolve().parents[3] / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))


def _load(name: str):
    path = TOOLS / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def functional_arguments() -> SimpleNamespace:
    return SimpleNamespace(
        pico_cdc0="cdc0",
        debug_probe="probe",
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
        uart0_pico="cdc0",
        uart0_peer="probe",
        uart1=None,
        uart1_peer=None,
        uart2="cdc2",
        uart3="cdc3",
        uart4=None,
        uart4_peer=None,
        uart5="cdc5",
        uart0_baud=115200,
        rates="115200,460800",
        duration=10.0,
        payload_bytes=1024,
        timeout=3.0,
    )


def test_functional_runner_builds_all_documented_stages() -> None:
    runner = _load("run_functional_test")
    commands = runner.build_stage_commands(functional_arguments())

    assert [label for label, _ in commands] == [
        "Debug Probe to HW UART0",
        "HW UART1 to PIO UART2",
        "PIO UART3 to PIO UART4",
        "PIO UART5 loopback",
    ]
    assert "--loopback" in commands[-1][1]
    assert "--peer-port" in commands[1][1]


def test_functional_runner_selects_one_stage() -> None:
    runner = _load("run_functional_test")
    arguments = functional_arguments()
    arguments.stage = "2"

    commands = runner.build_stage_commands(arguments)

    assert [label for label, _ in commands] == ["HW UART1 to PIO UART2"]


def test_hardware_runner_streams_child_output_and_status() -> None:
    runner = _load("run_hardware_test")
    command = [sys.executable, "-c", "print('child-output', flush=True)"]

    status, transcript = runner.run_child("stream check", command)

    assert status == 0
    assert "Command:" in transcript
    assert "child-output" in transcript


def test_hid_health_module_resolves_repository_root() -> None:
    health = _load("hardware_test_health")

    assert health.REPO_ROOT == Path(__file__).resolve().parents[3]
    assert health.HID_TOOL == health.REPO_ROOT / "host/python/src/pico_uart_hid.py"


def test_single_functional_stage_is_recorded_partial() -> None:
    runner = _load("run_functional_test")
    arguments = SimpleNamespace(
        board="pico", firmware_version="0.0.0", firmware_commit="local",
        baud=115200, payload_bytes=64,
    )
    clean = {
        "channels": {index: 1 for index in range(6)},
        "overruns": {index: 0 for index in range(6)},
        "error": None,
    }

    entry = runner.format_result_entry(
        arguments, "2026-09-20T00:00:00+00:00",
        [("HW UART1 to PIO UART2", 0, "PASS")], clean, clean)

    assert "**Result:** `PARTIAL`" in entry


def test_performance_runner_parses_pass_and_fail_lines() -> None:
    runner = _load("run_performance_test")
    output = (
        "PASS uart0-pico-to-peer: 100 bytes, 20.0 B/s\n"
        "FAIL uart5-loopback: received data did not match\n"
    )

    assert runner.parse_benchmark_output(output) == {
        "uart0-pico-to-peer": ("PASS", "100", "20.0"),
        "uart5-loopback": ("FAIL", "-", "received data did not match"),
    }


def test_performance_runner_accepts_documented_peer_options(monkeypatch) -> None:
    runner = _load("run_performance_test")
    monkeypatch.setattr(sys, "argv", [
        "run_performance_test.py",
        "--uart0-pico", "cdc0", "--uart0-peer", "probe",
        "--uart1", "cdc1", "--uart1-peer", "cdc2",
        "--uart2", "cdc2", "--uart3", "cdc3",
        "--uart4", "cdc4", "--uart4-peer", "cdc3",
        "--uart5", "cdc5",
    ])

    arguments = runner.parse_arguments()
    command = runner.build_command(arguments)

    assert arguments.uart1_peer == "cdc2"
    assert arguments.uart4_peer == "cdc3"
    assert command[-4:] == ["--uart4", "cdc4", "--uart4-peer", "cdc3"]


def test_full_hardware_runner_defaults_to_usb_sustainable_rate(monkeypatch) -> None:
    runner = _load("run_hardware_test")
    arguments = ["run_hardware_test.py", "--pico-cdc0", "cdc0", "--debug-probe", "probe",
                 "--pico-cdc2", "cdc2", "--pico-cdc3", "cdc3", "--pico-cdc5", "cdc5"]
    monkeypatch.setattr(sys, "argv", arguments)

    assert runner.parse_arguments().rates == "115200"


def test_performance_runner_preserves_rate_results() -> None:
    runner = _load("run_performance_test")
    output = (
        "Benchmarking PIO/loopbacks at 115200 baud; UART0 at 115200 baud\n"
        "PASS uart5-loopback: 100 bytes, 20.0 B/s\n"
        "Benchmarking PIO/loopbacks at 1000000 baud; UART0 at 115200 baud\n"
        "FAIL uart5-loopback: timeout\n"
    )

    assert runner.parse_benchmark_output_by_rate(output) == {
        (115200, "uart5-loopback"): ("PASS", "100", "20.0"),
        (1000000, "uart5-loopback"): ("FAIL", "-", "timeout"),
    }


def test_hardware_runner_marks_failed_functional_phase_as_fail() -> None:
    runner = _load("run_hardware_test")
    arguments = SimpleNamespace(
        board="pico", tester="test", firmware_version="1.2.3",
        firmware_commit="abc1234",
    )

    entry = runner.format_result_entry(arguments, "2026-09-20T00:00:00+00:00",
                                       (1, "functional failed"), None)

    assert "**Result:** `FAIL`" in entry


def test_result_helper_prepends_before_template(tmp_path: Path) -> None:
    helper = _load("hardware_test_result")
    results = tmp_path / "results.md"
    results.write_text("# Results\n\n## Template\n", encoding="utf-8")

    helper.prepend_result(results, "## New result\n\n**Result:** `PASS`")

    assert results.read_text(encoding="utf-8") == (
        "# Results\n\n## New result\n\n**Result:** `PASS`\n\n## Template\n"
    )


def test_result_helper_rejects_missing_template(tmp_path: Path) -> None:
    helper = _load("hardware_test_result")
    results = tmp_path / "results.md"
    results.write_text("# Results\n", encoding="utf-8")

    with pytest.raises(ValueError, match="template marker"):
        helper.prepend_result(results, "## New result")
