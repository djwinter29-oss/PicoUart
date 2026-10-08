#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

BRIDGE = Path(__file__).resolve().parents[2] / "tools" / "hardware" / "serial_bridge_test.py"

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
    """A real OSError from os.write (not BlockingIOError backpressure) must propagate."""
    bridge = _load_bridge()

    monkeypatch.setattr(bridge.select, "select", lambda r, w, x, _t: ([], w, []))

    def fail_write(*_args):
        raise OSError("device disconnected")

    monkeypatch.setattr(bridge.os, "write", fail_write)

    with pytest.raises(OSError, match="device disconnected"):
        bridge.run_flood(3, None, 1.0, 64, 0.0)


def test_run_flood_drains_during_write_backpressure(monkeypatch: pytest.MonkeyPatch) -> None:
    """Destination must be drained even while the source write is backpressured.

    Regression for the write_all-based implementation, which blocked on the
    whole-deadline write and could never service destination_fd while a write
    was stalled, backing up the loopback receive side.
    """
    bridge = _load_bridge()
    clock = {"t": 0.0}
    monkeypatch.setattr(bridge.time, "monotonic", lambda: clock["t"])
    monkeypatch.setattr(bridge.time, "sleep", lambda _s: None)

    calls = {"n": 0}

    def fake_select(read_fds, _write_fds, _err, _timeout):
        # Calls 1-2 are the flood loop's own select plus drain_available's
        # nested select; both report the destination readable (one chunk
        # available) while the write side stays empty (backpressured).
        # Every later call (further flood iterations and the settle drain)
        # reports nothing readable/writable so the test terminates quickly.
        calls["n"] += 1
        clock["t"] += 0.01
        if calls["n"] <= 2:
            return (read_fds, [], [])
        return ([], [], [])

    monkeypatch.setattr(bridge.select, "select", fake_select)
    read_chunks = iter([b"drained-bytes"])
    monkeypatch.setattr(bridge.os, "read", lambda *_a: next(read_chunks, b""))
    monkeypatch.setattr(bridge.os, "write", lambda *_a: (_ for _ in ()).throw(AssertionError(
        "write must not be attempted while backpressured"
    )))

    written, drained = bridge.run_flood(3, 4, 1.0, 64, 0.0)

    assert drained == len(b"drained-bytes")
    assert written == 0


def test_run_flood_counts_partial_write_and_ends_at_deadline(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Deadline expiry mid-write is normal termination; only accepted bytes count."""
    bridge = _load_bridge()
    clock = {"t": 0.0}
    monkeypatch.setattr(bridge.time, "monotonic", lambda: clock["t"])
    monkeypatch.setattr(bridge.time, "sleep", lambda _s: None)
    monkeypatch.setattr(bridge.select, "select", lambda r, w, x, _t: ([], w, []))

    def partial_write(_fd, data):
        clock["t"] += 0.4  # deadline check happens before each write, not after
        return len(data[:5])

    monkeypatch.setattr(bridge.os, "write", partial_write)

    written, drained = bridge.run_flood(3, None, 1.0, 64, 0.0)

    # Three writes run (checks at t=0, 0.4, 0.8 all pass before the 1.0s
    # deadline; the fourth check at t=1.2 stops the loop), each accepting
    # only 5 of the 64 pattern bytes offered -> 15, never a 64-multiple.
    assert written == 15
    assert drained == 0


def test_run_flood_test_rejects_zero_write(monkeypatch: pytest.MonkeyPatch) -> None:
    """Zero bytes written must FAIL, never a false PASS, regardless of drained count."""
    bridge = _load_bridge()
    arguments = type(
        "Arguments",
        (),
        {
            "pico_port": "/dev/fake",
            "peer_port": "/dev/fake2",
            "loopback": False,
            "settle_seconds": 0.0,
            "label": "test",
            "payload_bytes": 64,
            "flood_seconds": 1.0,
            "hold_cdc_seconds": 0.0,
        },
    )()
    monkeypatch.setattr(bridge, "configure_port", lambda *_a: (17, []))
    monkeypatch.setattr(bridge, "run_flood", lambda *_a, **_k: (0, 0))
    monkeypatch.setattr(bridge, "close_ports", lambda *_a: None)

    assert bridge.run_flood_test(arguments, 115200) == 1


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


@pytest.mark.parametrize("wake_at", [1.0, 1.1])
def test_flood_does_not_write_after_select_deadline(monkeypatch, wake_at):
    bridge = _load_bridge()
    clock = [0.0]
    monkeypatch.setattr(bridge.time, "monotonic", lambda: clock[0])

    def ready(_r, w, _x, _timeout):
        clock[0] = wake_at
        return [], w, []

    monkeypatch.setattr(bridge.select, "select", ready)
    monkeypatch.setattr(bridge.os, "write", lambda *_: pytest.fail("late write"))
    assert bridge.run_flood(3, None, 1, 64, 0) == (0, 0)


def test_flood_rejects_zero_progress_write(monkeypatch):
    bridge = _load_bridge()
    monkeypatch.setattr(bridge.select, "select", lambda _r, w, _x, _t: ([], w, []))
    monkeypatch.setattr(bridge.os, "write", lambda *_: 0)
    with pytest.raises(OSError, match="zero bytes"):
        bridge.run_flood(3, None, 1, 64, 0)


def test_flood_retries_blocking_write_and_counts_only_progress(monkeypatch):
    bridge = _load_bridge()
    clock = [0.0]
    calls = []
    monkeypatch.setattr(bridge.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(bridge.select, "select", lambda _r, w, _x, _t: ([], w, []))

    def write(_fd, data):
        calls.append(data)
        clock[0] += 0.6
        if len(calls) == 1:
            raise BlockingIOError()
        return 5

    monkeypatch.setattr(bridge.os, "write", write)
    assert bridge.run_flood(3, None, 1, 64, 0) == (5, 0)
    assert calls[0] == calls[1]


@pytest.mark.parametrize("wake_at", [1.0, 1.1])
def test_drain_checks_deadline_after_select(monkeypatch, wake_at):
    bridge = _load_bridge()
    clock = [0.0]
    monkeypatch.setattr(bridge.time, "monotonic", lambda: clock[0])

    def ready(r, _w, _x, timeout):
        assert timeout == 0
        clock[0] = wake_at
        return r, [], []

    monkeypatch.setattr(bridge.select, "select", ready)
    monkeypatch.setattr(bridge.os, "read", lambda *_: pytest.fail("late read"))
    assert bridge.drain_available(4, 1) == 0


@pytest.mark.parametrize("stop", ["deadline", "budget", "empty", "blocking"])
def test_drain_is_bounded_with_continuously_readable_input(monkeypatch, stop):
    bridge = _load_bridge()
    clock = [0.0]
    reads = []
    monkeypatch.setattr(bridge.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(bridge.select, "select", lambda r, _w, _x, _t: (r, [], []))

    def read(_fd, size):
        reads.append(size)
        if stop == "deadline":
            clock[0] += 0.5
        if stop == "empty":
            return b""
        if stop == "blocking":
            raise BlockingIOError()
        return b"x" * size

    monkeypatch.setattr(bridge.os, "read", read)
    drained = bridge.drain_available(4, 1, max_reads=3)
    count = {"deadline": 2, "budget": 3, "empty": 1, "blocking": 1}[stop]
    assert len(reads) == count
    assert drained == (count * 4096 if stop in ("deadline", "budget") else 0)


def test_continuous_rx_allows_tx_and_bounded_settle(monkeypatch):
    bridge = _load_bridge()
    clock = [0.0]
    writes = []
    read_times = []
    monkeypatch.setattr(bridge.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(bridge.select, "select", lambda r, w, _x, _t: (r, w, []))

    def read(_fd, size):
        read_times.append(clock[0])
        clock[0] += 0.001
        return b"x" * size

    def write(_fd, data):
        writes.append((clock[0], data))
        return len(data)

    monkeypatch.setattr(bridge.os, "read", read)
    monkeypatch.setattr(bridge.os, "write", write)
    written, drained = bridge.run_flood(3, 4, 0.05, 64, 0)
    assert written == 3 * 64
    assert all(now < 0.05 for now, _ in writes)
    assert drained == len(read_times) * 4096
    assert clock[0] < 1.052
    assert all(now < 1.051 for now in read_times)


def test_drain_expiry_prevents_ready_write(monkeypatch):
    bridge = _load_bridge()
    clock = [0.0]
    monkeypatch.setattr(bridge.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(bridge.select, "select", lambda r, w, _x, _t: (r, w, []))

    def drain(_fd, deadline):
        clock[0] = deadline
        return 4

    monkeypatch.setattr(bridge, "drain_available", drain)
    monkeypatch.setattr(bridge.os, "write", lambda *_: pytest.fail("write after drain deadline"))
    assert bridge.run_flood(3, 4, 1, 64, 0) == (0, 8)
