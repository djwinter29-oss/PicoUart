#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

STRESS = Path(__file__).resolve().parents[3] / "tools" / "linux" / "serial_stress_benchmark.py"

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux serial tools import termios")


def _load_stress():
    spec = importlib.util.spec_from_file_location("serial_stress_benchmark_under_test", STRESS)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


@pytest.mark.parametrize("failure", ["tcgetattr", "tcsetattr", "tcflush"])
def test_configure_port_closes_once_on_failure(
    monkeypatch: pytest.MonkeyPatch, failure: str
) -> None:
    stress = _load_stress()
    closes = []
    get_calls = 0

    def tcgetattr(_fd):
        nonlocal get_calls
        get_calls += 1
        if failure == "tcgetattr" and get_calls == 1:
            raise OSError("get failed")
        return [0, 0, 0, 0, 0, 0, [0] * 32]

    monkeypatch.setattr(stress.os, "open", lambda *_args: 19)
    monkeypatch.setattr(stress.os, "close", closes.append)
    monkeypatch.setattr(stress.termios, "tcgetattr", tcgetattr)
    monkeypatch.setattr(
        stress.termios,
        "tcsetattr",
        lambda *_args: (_ for _ in ()).throw(OSError("set failed"))
        if failure == "tcsetattr" else None,
    )
    monkeypatch.setattr(
        stress.termios,
        "tcflush",
        lambda *_args: (_ for _ in ()).throw(OSError("flush failed"))
        if failure == "tcflush" else None,
    )

    with pytest.raises(OSError):
        stress.configure_port("/dev/fake", 115200)
    assert closes == [19]


def test_configure_port_success_remains_open(monkeypatch: pytest.MonkeyPatch) -> None:
    stress = _load_stress()
    closes = []
    settings = [0, 0, 0, 0, 0, 0, [0] * 32]
    monkeypatch.setattr(stress.os, "open", lambda *_args: 19)
    monkeypatch.setattr(stress.os, "close", closes.append)
    monkeypatch.setattr(stress.termios, "tcgetattr", lambda _fd: settings.copy())
    monkeypatch.setattr(stress.termios, "tcsetattr", lambda *_args: None)
    monkeypatch.setattr(stress.termios, "tcflush", lambda *_args: None)

    file_descriptor, _ = stress.configure_port("/dev/fake", 115200)

    assert file_descriptor == 19
    assert closes == []


def test_close_ports_closes_all_after_restore_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    stress = _load_stress()
    closes = []

    def restore(file_descriptor, *_args):
        if file_descriptor == 19:
            raise OSError("restore failed")

    monkeypatch.setattr(stress.termios, "tcsetattr", restore)
    monkeypatch.setattr(stress.os, "close", closes.append)

    error = stress.close_ports([(19, []), (20, [])])
    assert isinstance(error, OSError)
    assert str(error) == "restore failed"
    assert closes == [20, 19]


def test_benchmark_reports_cleanup_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    stress = _load_stress()
    arguments = type(
        "Arguments",
        (),
        {
            "uart0_pico": "/dev/uart0-pico",
            "uart0_peer": "/dev/uart0-peer",
            "uart1": None,
            "uart2": "/dev/uart2",
            "uart3": "/dev/uart3",
            "uart4": None,
            "uart5": "/dev/uart5",
            "uart0_baud": 115200,
            "duration": 0.1,
            "payload_bytes": 64,
            "timeout": 1.0,
        },
    )()
    next_descriptor = iter(range(20, 25))

    class ImmediateThread:
        def __init__(self, target, args):
            self.target = target
            self.args = args

        def start(self):
            self.target(*self.args)

        def join(self):
            return None

    def complete_stream(label, _source, _destination, _duration, _payload, _timeout, _start, result):
        result[label] = (64, None)

    monkeypatch.setattr(stress, "configure_port", lambda *_args: (next(next_descriptor), []))
    monkeypatch.setattr(stress, "run_stream", complete_stream)
    monkeypatch.setattr(stress.threading, "Thread", ImmediateThread)
    monkeypatch.setattr(stress, "close_ports", lambda *_args: OSError("restore failed"))

    assert stress.benchmark_rate(arguments, 115200) is False


@pytest.mark.parametrize(("option", "value"), [("--duration", "nan"), ("--timeout", "inf")])
def test_non_finite_timing_is_rejected(
    monkeypatch: pytest.MonkeyPatch, option: str, value: str
) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_stress_benchmark.py",
            "--uart0-pico", "/dev/null",
            "--uart0-peer", "/dev/null",
            "--uart2", "/dev/null",
            "--uart3", "/dev/null",
            "--uart5", "/dev/null",
            option, value,
        ],
    )
    stress = _load_stress()
    assert stress.main() == 2


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


def test_minimum_payload_contains_distinct_sequence_marker() -> None:
    stress = _load_stress()

    first = stress.payload_for("uart2-to-uart3", 0, 32)
    second = stress.payload_for("uart2-to-uart3", 1, 32)

    assert len(first) == 32
    assert len(second) == 32
    assert first != second


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


def test_cross_fixture_arguments_parse(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_stress_benchmark.py",
            "--uart0-pico", "/dev/ttyACM0", "--uart0-peer", "/dev/ttyACM7",
            "--uart1", "/dev/ttyACM1", "--uart1-peer", "/dev/ttyACM2",
            "--uart2", "/dev/ttyACM2", "--uart3", "/dev/ttyACM3",
            "--uart4", "/dev/ttyACM4", "--uart4-peer", "/dev/ttyACM3",
            "--uart5", "/dev/ttyACM5",
        ],
    )
    stress = _load_stress()
    args = stress.parse_arguments()
    assert args.uart1_peer == "/dev/ttyACM2"
    assert args.uart4_peer == "/dev/ttyACM3"
    monkeypatch.setattr(stress, "benchmark_rate", lambda *_a, **_k: True)
    assert stress.main() == 0
