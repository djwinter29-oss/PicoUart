#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

BRIDGE = Path(__file__).resolve().parents[3] / "tools" / "serial_bridge_test.py"

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux serial tools import termios")


def _load_bridge():
    spec = importlib.util.spec_from_file_location("serial_bridge_test_under_test", BRIDGE)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_write_all_rejects_zero_progress(monkeypatch: pytest.MonkeyPatch) -> None:
    bridge = _load_bridge()
    monkeypatch.setattr(bridge.os, "write", lambda *_args: 0)

    with pytest.raises(OSError, match="zero bytes"):
        bridge.write_all(3, b"payload", bridge.time.monotonic() + 1.0)


def test_write_all_honors_deadline(monkeypatch: pytest.MonkeyPatch) -> None:
    bridge = _load_bridge()
    monkeypatch.setattr(
        bridge.os,
        "write",
        lambda *_args: (_ for _ in ()).throw(BlockingIOError()),
    )
    monkeypatch.setattr(bridge.time, "monotonic", lambda: 10.0)

    with pytest.raises(TimeoutError, match="write timed out"):
        bridge.write_all(3, b"payload", 10.0)


@pytest.mark.parametrize("failure", ["tcgetattr", "tcsetattr", "tcflush"])
def test_configure_port_closes_once_on_failure(
    monkeypatch: pytest.MonkeyPatch, failure: str
) -> None:
    bridge = _load_bridge()
    closes = []
    get_calls = 0

    def tcgetattr(_fd):
        nonlocal get_calls
        get_calls += 1
        if failure == "tcgetattr" and get_calls == 1:
            raise OSError("get failed")
        return [0, 0, 0, 0, 0, 0, [0] * 32]

    monkeypatch.setattr(bridge.os, "open", lambda *_args: 17)
    monkeypatch.setattr(bridge.os, "close", closes.append)
    monkeypatch.setattr(bridge.termios, "tcgetattr", tcgetattr)
    monkeypatch.setattr(
        bridge.termios,
        "tcsetattr",
        lambda *_args: (_ for _ in ()).throw(OSError("set failed"))
        if failure == "tcsetattr" else None,
    )
    monkeypatch.setattr(
        bridge.termios,
        "tcflush",
        lambda *_args: (_ for _ in ()).throw(OSError("flush failed"))
        if failure == "tcflush" else None,
    )

    with pytest.raises(OSError):
        bridge.configure_port("/dev/fake", 115200)
    assert closes == [17]


def test_configure_port_success_remains_open(monkeypatch: pytest.MonkeyPatch) -> None:
    bridge = _load_bridge()
    closes = []
    settings = [0, 0, 0, 0, 0, 0, [0] * 32]
    monkeypatch.setattr(bridge.os, "open", lambda *_args: 17)
    monkeypatch.setattr(bridge.os, "close", closes.append)
    monkeypatch.setattr(bridge.termios, "tcgetattr", lambda _fd: settings.copy())
    monkeypatch.setattr(bridge.termios, "tcsetattr", lambda *_args: None)
    monkeypatch.setattr(bridge.termios, "tcflush", lambda *_args: None)

    file_descriptor, _ = bridge.configure_port("/dev/fake", 115200)

    assert file_descriptor == 17
    assert closes == []


def test_close_ports_closes_all_after_restore_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    bridge = _load_bridge()
    closes = []

    def restore(file_descriptor, *_args):
        if file_descriptor == 17:
            raise OSError("restore failed")

    monkeypatch.setattr(bridge.termios, "tcsetattr", restore)
    monkeypatch.setattr(bridge.os, "close", closes.append)

    error = bridge.close_ports([(17, []), (18, [])])
    assert isinstance(error, OSError)
    assert str(error) == "restore failed"
    assert closes == [17, 18]


def test_run_test_reports_cleanup_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    bridge = _load_bridge()
    arguments = type(
        "Arguments",
        (),
        {
            "pico_port": "/dev/fake",
            "loopback": True,
            "settle_seconds": 0.0,
            "label": "test",
            "payload_bytes": 64,
            "timeout": 1.0,
        },
    )()
    monkeypatch.setattr(bridge, "configure_port", lambda *_args: (17, []))
    monkeypatch.setattr(bridge, "test_direction", lambda *_args: True)
    monkeypatch.setattr(bridge, "close_ports", lambda *_args: OSError("restore failed"))

    assert bridge.run_test(arguments, 115200) == 2


@pytest.mark.parametrize(("option", "value"), [("--timeout", "nan"), ("--settle-seconds", "inf")])
def test_non_finite_timing_is_rejected(
    monkeypatch: pytest.MonkeyPatch, option: str, value: str
) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        ["serial_bridge_test.py", "--pico-port", "/dev/null", "--loopback", option, value],
    )
    bridge = _load_bridge()
    assert bridge.main() == 2


def test_payload_bytes_rejects_above_max(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_bridge_test.py",
            "--pico-port",
            "/dev/null",
            "--loopback",
            "--payload-bytes",
            "65536",
        ],
    )
    bridge = _load_bridge()
    assert bridge.main() == 2


def test_payload_bytes_accepts_max(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_bridge_test.py",
            "--pico-port",
            "/dev/null",
            "--loopback",
            "--payload-bytes",
            "4096",
        ],
    )
    bridge = _load_bridge()
    monkeypatch.setattr(bridge, "run_test", lambda *_args, **_kwargs: 0)
    assert bridge.main() == 0
    args = bridge.parse_arguments()
    assert args.payload_bytes == 4096


def test_hold_cdc_requires_flood(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_bridge_test.py",
            "--pico-port",
            "/dev/null",
            "--peer-port",
            "/dev/null",
            "--hold-cdc-seconds",
            "1",
        ],
    )
    bridge = _load_bridge()
    assert bridge.main() == 2


def test_flood_seconds_parses(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_bridge_test.py",
            "--pico-port",
            "/dev/null",
            "--peer-port",
            "/dev/null",
            "--flood-seconds",
            "2.5",
            "--hold-cdc-seconds",
            "1",
            "--payload-bytes",
            "64",
        ],
    )
    bridge = _load_bridge()
    args = bridge.parse_arguments()
    assert args.flood_seconds == 2.5
    assert args.hold_cdc_seconds == 1.0
    monkeypatch.setattr(bridge, "run_flood_test", lambda *_args, **_kwargs: 0)
    assert bridge.main() == 0


def test_flood_propagates_write_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    bridge = _load_bridge()

    def fail_write(*_args):
        raise OSError("device disconnected")

    monkeypatch.setattr(bridge, "write_all", fail_write)

    with pytest.raises(OSError, match="device disconnected"):
        bridge.run_flood(3, None, 1.0, 64, 0.0)


def test_settle_seconds_rejects_negative(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_bridge_test.py",
            "--pico-port",
            "/dev/null",
            "--loopback",
            "--settle-seconds",
            "-0.1",
        ],
    )
    bridge = _load_bridge()
    assert bridge.main() == 2


def test_settle_seconds_parses(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_bridge_test.py",
            "--pico-port",
            "/dev/null",
            "--loopback",
            "--settle-seconds",
            "0.2",
        ],
    )
    bridge = _load_bridge()
    args = bridge.parse_arguments()
    assert args.settle_seconds == 0.2
    monkeypatch.setattr(bridge, "run_test", lambda *_args, **_kwargs: 0)
    assert bridge.main() == 0


def test_flood_loopback_sleeps_settle_seconds(monkeypatch: pytest.MonkeyPatch) -> None:
    """Flood path must honor --settle-seconds (not a hardcoded 0.05)."""
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_bridge_test.py",
            "--pico-port",
            "/dev/null",
            "--loopback",
            "--flood-seconds",
            "0.1",
            "--settle-seconds",
            "0.2",
            "--payload-bytes",
            "64",
        ],
    )
    bridge = _load_bridge()
    args = bridge.parse_arguments()
    assert args.settle_seconds == 0.2
    assert args.flood_seconds == 0.1

    sleeps: list[float] = []
    monkeypatch.setattr(bridge.time, "sleep", lambda seconds: sleeps.append(seconds))
    monkeypatch.setattr(bridge, "configure_port", lambda *_a, **_k: (3, object()))
    monkeypatch.setattr(bridge, "run_flood", lambda *_a, **_k: (100, 100))
    monkeypatch.setattr(bridge.os, "close", lambda *_a, **_k: None)
    monkeypatch.setattr(bridge.termios, "tcsetattr", lambda *_a, **_k: None)

    assert bridge.run_flood_test(args, 115200) == 0
    assert sleeps.count(0.2) == 1
    assert 0.05 not in sleeps


def test_flood_hold_cdc_sleeps_settle_seconds(monkeypatch: pytest.MonkeyPatch) -> None:
    """Peer + hold-CDC flood must settle before held TX and again after CDC open."""
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "serial_bridge_test.py",
            "--pico-port",
            "/dev/null",
            "--peer-port",
            "/dev/null",
            "--flood-seconds",
            "1.0",
            "--hold-cdc-seconds",
            "0.5",
            "--settle-seconds",
            "0.2",
            "--payload-bytes",
            "64",
        ],
    )
    bridge = _load_bridge()
    args = bridge.parse_arguments()
    assert args.settle_seconds == 0.2
    assert args.hold_cdc_seconds == 0.5

    sleeps: list[float] = []
    monkeypatch.setattr(bridge.time, "sleep", lambda seconds: sleeps.append(seconds))
    monkeypatch.setattr(bridge, "configure_port", lambda *_a, **_k: (3, object()))
    monkeypatch.setattr(bridge, "run_flood", lambda *_a, **_k: (100, 100))
    monkeypatch.setattr(bridge.os, "close", lambda *_a, **_k: None)
    monkeypatch.setattr(bridge.termios, "tcsetattr", lambda *_a, **_k: None)

    assert bridge.run_flood_test(args, 115200) == 0
    assert sleeps.count(0.2) == 2
    assert 0.05 not in sleeps
