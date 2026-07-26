#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

BRIDGE = Path(__file__).resolve().parents[3] / "tools" / "linux" / "serial_bridge_test.py"


def _load_bridge():
    spec = importlib.util.spec_from_file_location("serial_bridge_test_under_test", BRIDGE)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


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
    assert 0.2 in sleeps
    assert 0.05 not in sleeps
