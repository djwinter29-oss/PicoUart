#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

STRESS = Path(__file__).resolve().parents[3] / "tools" / "linux" / "serial_stress_benchmark.py"


def _load_stress():
    spec = importlib.util.spec_from_file_location("serial_stress_benchmark_under_test", STRESS)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_payload_bytes_rejects_out_of_range(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_stress_benchmark.py",
            "--uart0-pico",
            "/dev/null",
            "--uart0-peer",
            "/dev/null",
            "--uart2",
            "/dev/null",
            "--uart3",
            "/dev/null",
            "--uart5",
            "/dev/null",
            "--payload-bytes",
            "16",
        ],
    )
    stress = _load_stress()
    assert stress.main() == 2


def test_optional_uart1_uart4_parse(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_stress_benchmark.py",
            "--uart0-pico",
            "/dev/null",
            "--uart0-peer",
            "/dev/null",
            "--uart1",
            "/dev/ttyACM1",
            "--uart2",
            "/dev/null",
            "--uart3",
            "/dev/null",
            "--uart4",
            "/dev/ttyACM4",
            "--uart5",
            "/dev/null",
            "--rates",
            "115200",
            "--duration",
            "0.1",
        ],
    )
    stress = _load_stress()
    args = stress.parse_arguments()
    assert args.uart1 == "/dev/ttyACM1"
    assert args.uart4 == "/dev/ttyACM4"
    monkeypatch.setattr(stress, "benchmark_rate", lambda *_a, **_k: True)
    assert stress.main() == 0
