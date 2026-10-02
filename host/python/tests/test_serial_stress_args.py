#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

STRESS = Path(__file__).resolve().parents[3] / "tools" / "hardware" / "serial_stress_benchmark.py"

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


def test_run_stream_collects_first_transfer_timing(monkeypatch: pytest.MonkeyPatch) -> None:
    stress = _load_stress()
    ticks = iter([0, 0, 0, 0.1, 0.2, 0.3, 0.4, 1])
    monkeypatch.setattr(stress.time, "monotonic", lambda: next(ticks))
    transfers = []
    monkeypatch.setattr(stress, "write_all", lambda fd, payload, deadline: transfers.append(payload))
    def read_exact(fd, payload, deadline, timing=None):
        timing["first_receive_utc"] = stress.datetime.now(stress.timezone.utc).isoformat(timespec="milliseconds")
        timing["first_receive_monotonic"] = stress.time.monotonic()

    monkeypatch.setattr(stress, "read_exact", read_exact)
    result, timing = {}, {}
    barrier = type("Barrier", (), {"wait": lambda self: None})()
    stress.run_stream("test", 1, 2, 0.5, 64, 1, barrier, result, timing)
    assert result["test"] == (64, None)
    assert len(transfers) == 1
    stamps = timing["test"]
    for event in ("thread_start", "first_send", "first_receive"):
        assert stamps[f"{event}_utc"].endswith("+00:00")
        assert isinstance(stamps[f"{event}_monotonic"], (int, float))
    assert stamps["thread_start_monotonic"] <= stamps["first_send_monotonic"] <= stamps["first_receive_monotonic"]


@pytest.mark.parametrize("outcome", ["success", "timeout", "mismatch", "no_data"])
def test_run_stream_records_first_nonempty_read(
    monkeypatch: pytest.MonkeyPatch, outcome: str
) -> None:
    from datetime import datetime, timedelta, timezone

    stress = _load_stress()
    clock = [0.0]
    epoch = datetime(2026, 1, 1, tzinfo=timezone.utc)

    class ControlledDatetime:
        @staticmethod
        def now(tz):
            assert tz == timezone.utc
            return epoch + timedelta(seconds=clock[0])

    first = stress.payload_for("test", 0, 64)
    second = stress.payload_for("test", 1, 64)
    chunks = [(1.0, b"")]
    if outcome != "no_data":
        chunks.append((2.0, first[:8]))
    if outcome in ("success", "mismatch"):
        tail = first[8:] if outcome == "success" else b"!" * 56
        chunks.append((3.0, tail))
    if outcome == "success":
        chunks.extend([(4.0, second[:8]), (6.0, second[8:])])
    reads = []
    waits = []
    writes = []

    def select_read(readable, writable, exceptional, remaining):
        assert (readable, writable, exceptional) == ([2], [], [])
        waits.append((clock[0], remaining))
        if chunks:
            clock[0] = chunks[0][0]
            return [2], [], []
        clock[0] += remaining
        return [], [], []

    def read(fd, size):
        assert fd == 2
        _, chunk = chunks.pop(0)
        assert len(chunk) <= size
        reads.append(size)
        return chunk

    def write(fd, data):
        assert fd == 1
        writes.append(data)
        return len(data)

    monkeypatch.setattr(stress.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(stress, "datetime", ControlledDatetime)
    monkeypatch.setattr(stress.select, "select", select_read)
    monkeypatch.setattr(stress.os, "read", read)
    monkeypatch.setattr(stress.os, "write", write)
    result, timing = {}, {}
    barrier = type("Barrier", (), {"wait": lambda self: None})()

    stress.run_stream("test", 1, 2, 5, 64, 10, barrier, result, timing)

    expected_results = {
        "success": (128, None),
        "timeout": (0, "received 8 of 64 bytes"),
        "mismatch": (0, "received data did not match transmitted data"),
        "no_data": (0, "received 0 of 64 bytes"),
    }
    assert result["test"] == expected_results[outcome]
    assert writes == ([first, second] if outcome == "success" else [first])
    assert reads == {
        "success": [64, 64, 56, 64, 56],
        "timeout": [64, 64],
        "mismatch": [64, 64, 56],
        "no_data": [64],
    }[outcome]
    # Each payload retains its original read deadline across empty/partial reads.
    assert all(now + remaining == (13 if now >= 3 else 10)
               for now, remaining in waits)
    stamps = timing["test"]
    if outcome == "no_data":
        assert "first_receive_monotonic" not in stamps
        assert "first_receive_utc" not in stamps
    else:
        assert stamps["first_receive_monotonic"] == 2.0
        assert stamps["first_receive_utc"] == "2026-01-01T00:00:02.000+00:00"


def test_benchmark_rate_collects_and_prints_stream_timing(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """Each stream's timing dict (collected by run_stream) must survive into
    benchmark_rate's final report as a ``TIME <label>: {...}`` line, with the
    thread-start/first-send/first-receive keys run_stream is expected to set.
    """
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
            "settle_seconds": 0.0,
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

    expected_keys = {
        "thread_start_utc", "thread_start_monotonic",
        "first_send_utc", "first_send_monotonic",
        "first_receive_utc", "first_receive_monotonic",
    }

    def fake_run_stream(label, _source, _destination, _duration, _payload,
                         _timeout, _start, result, timing):
        timing[label] = {key: object() for key in expected_keys}
        result[label] = (64, None)

    monkeypatch.setattr(stress, "configure_port", lambda *_args: (next(next_descriptor), []))
    monkeypatch.setattr(stress, "run_stream", fake_run_stream)
    monkeypatch.setattr(stress.threading, "Thread", ImmediateThread)
    monkeypatch.setattr(stress, "close_ports", lambda *_args: None)

    assert stress.benchmark_rate(arguments, 115200) is True

    stdout = capsys.readouterr().out
    time_lines = [line for line in stdout.splitlines() if line.startswith("TIME ")]
    stream_labels = ["uart0-pico-to-peer", "uart0-peer-to-pico",
                      "uart5-loopback", "uart2-to-uart3", "uart3-to-uart2"]

    assert len(time_lines) == len(stream_labels)
    for label in stream_labels:
        matching = [line for line in time_lines if line.startswith(f"TIME {label}:")]
        assert len(matching) == 1, f"missing or duplicate TIME line for {label}"
        for key in expected_keys:
            assert key in matching[0]


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

    def complete_stream(label, _source, _destination, _duration, _payload, _timeout, _start, result, _timing):
        result[label] = (64, None)

    monkeypatch.setattr(stress, "configure_port", lambda *_args: (next(next_descriptor), []))
    monkeypatch.setattr(stress, "run_stream", complete_stream)
    monkeypatch.setattr(stress.threading, "Thread", ImmediateThread)
    monkeypatch.setattr(stress, "close_ports", lambda *_args: OSError("restore failed"))

    assert stress.benchmark_rate(arguments, 115200) is False


@pytest.mark.parametrize(("option", "value"), [
    ("--duration", "nan"), ("--timeout", "inf"),
    ("--settle-seconds", "nan"), ("--settle-seconds", "inf"),
    ("--settle-seconds", "-1"),
])
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
    monkeypatch.setattr(stress, "benchmark_rate", lambda *_a, **_k: True)
    assert stress.main() == 0


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


def test_cross_fixture_rejects_mismatched_peer_paths() -> None:
    stress = _load_stress()
    arguments = type(
        "Arguments",
        (),
        {
            "uart1_peer": "/dev/ttyACM9",
            "uart2": "/dev/ttyACM2",
            "uart4_peer": "/dev/ttyACM3",
            "uart3": "/dev/ttyACM3",
        },
    )()

    assert stress.cross_fixture_paths_valid(arguments) is False


def test_cross_fixture_rejects_partial_peer_arguments() -> None:
    stress = _load_stress()
    arguments = type(
        "Arguments",
        (),
        {"uart1": "/dev/ttyACM1", "uart1_peer": "/dev/ttyACM2",
         "uart4": None, "uart4_peer": None},
    )()

    assert stress.cross_fixture_paths_valid(arguments) is False


def test_benchmark_allows_time_for_concurrent_line_coding() -> None:
    stress = _load_stress()

    assert stress.LINE_CODING_SETTLE_SECONDS >= 2.0


def test_performance_test_plan_documents_time_diagnostic_output(repo_root: Path) -> None:
    """The per-stream ``TIME <label>: {...}`` diagnostic line (undocumented
    output alongside the parsed PASS/FAIL lines) must be explained in the
    performance test plan so operators aren't left guessing at its format.
    """
    plan = (repo_root / "docs/tests/performance-test-plan.md").read_text()
    assert "TIME" in plan
    assert "run_performance_test.py parses" in plan or "not parsed by the runner" in plan


@pytest.mark.parametrize("wake_at", [1.0, 1.1])
def test_read_exact_rejects_first_byte_ready_at_deadline(monkeypatch, wake_at):
    stress = _load_stress()
    clock = [0.0]
    monkeypatch.setattr(stress.time, "monotonic", lambda: clock[0])

    def select_read(r, _w, _x, _timeout):
        clock[0] = wake_at
        return r, [], []

    monkeypatch.setattr(stress.select, "select", select_read)
    monkeypatch.setattr(stress.os, "read", lambda *_: pytest.fail("read after deadline"))
    timing = {}
    with pytest.raises(TimeoutError, match="received 0 of 4 bytes"):
        stress.read_exact(2, b"data", 1, timing)
    assert timing == {}


@pytest.mark.parametrize("finish_at", [1.0, 1.1])
@pytest.mark.parametrize("partial", [True, False])
def test_read_exact_rejects_late_completion_but_keeps_first_read_timing(
    monkeypatch, finish_at, partial
):
    stress = _load_stress()
    clock = [0.0]
    chunks = [(0.2, b"d"), (finish_at, b"ata")] if partial else [(finish_at, b"data")]
    monkeypatch.setattr(stress.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(stress.select, "select", lambda r, _w, _x, _t: (r, [], []))

    def read(_fd, _size):
        clock[0], chunk = chunks.pop(0)
        return chunk

    monkeypatch.setattr(stress.os, "read", read)
    timing = {}
    with pytest.raises(TimeoutError, match="received 4 of 4 bytes after deadline"):
        stress.read_exact(2, b"data", 1, timing)
    assert timing["first_receive_monotonic"] == (0.2 if partial else finish_at)
    assert timing["first_receive_utc"].endswith("+00:00")


def test_read_exact_accepts_completion_before_deadline(monkeypatch):
    stress = _load_stress()
    clock = [0.0]
    monkeypatch.setattr(stress.time, "monotonic", lambda: clock[0])

    def select_read(r, _w, _x, _t):
        clock[0] = 0.99
        return r, [], []

    monkeypatch.setattr(stress.select, "select", select_read)
    monkeypatch.setattr(stress.os, "read", lambda *_: b"data")
    timing = {}
    stress.read_exact(2, b"data", 1, timing)
    assert timing["first_receive_monotonic"] == 0.99


def test_write_all_does_not_pass_negative_select_timeout(monkeypatch):
    stress = _load_stress()
    clock = [0.0]
    monkeypatch.setattr(stress.time, "monotonic", lambda: clock[0])

    def write(*_):
        clock[0] = 1.1
        raise BlockingIOError()

    monkeypatch.setattr(stress.os, "write", write)
    monkeypatch.setattr(stress.select, "select", lambda *_: pytest.fail("expired select"))
    with pytest.raises(TimeoutError, match="write timed out"):
        stress.write_all(1, b"data", 1)


@pytest.mark.parametrize("wake_at", [1.0, 1.1])
def test_write_all_does_not_retry_after_select_deadline(monkeypatch, wake_at):
    stress = _load_stress()
    clock = [0.0]
    writes = []
    monkeypatch.setattr(stress.time, "monotonic", lambda: clock[0])

    def write(*_):
        writes.append(clock[0])
        raise BlockingIOError()

    def select_write(_r, w, _x, _t):
        clock[0] = wake_at
        return [], w, []

    monkeypatch.setattr(stress.os, "write", write)
    monkeypatch.setattr(stress.select, "select", select_write)
    with pytest.raises(TimeoutError, match="write timed out"):
        stress.write_all(1, b"data", 1)
    assert writes == [0.0]


def test_write_all_rejects_completion_at_deadline(monkeypatch):
    stress = _load_stress()
    clock = [0.0]
    monkeypatch.setattr(stress.time, "monotonic", lambda: clock[0])

    def write(_fd, data):
        clock[0] = 1
        return len(data)

    monkeypatch.setattr(stress.os, "write", write)
    with pytest.raises(TimeoutError, match="write timed out"):
        stress.write_all(1, b"data", 1)


@pytest.mark.parametrize(("uart1", "uart4", "cross"), [
    (False, False, False), (True, False, False),
    (False, True, False), (True, True, False), (True, True, True),
])
def test_benchmark_modes_configure_actual_documented_streams(monkeypatch, uart1, uart4, cross):
    from types import SimpleNamespace

    stress = _load_stress()
    arguments = SimpleNamespace(
        uart0_pico="cdc0", uart0_peer="probe", uart2="cdc2", uart3="cdc3", uart5="cdc5",
        uart1="cdc1" if uart1 else None, uart4="cdc4" if uart4 else None,
        uart1_peer="cdc2" if cross else None, uart4_peer="cdc3" if cross else None,
        uart0_baud=115200, duration=1, payload_bytes=64, timeout=1, settle_seconds=0,
    )
    opened = []
    streams = []

    def configure(path, _baud):
        opened.append(path)
        return len(opened), []

    class ImmediateThread:
        def __init__(self, target, args):
            self.target, self.args = target, args

        def start(self):
            self.target(*self.args)

        def join(self):
            pass

    def run_stream(label, source, destination, _duration, _payload, _timeout, _start, results, _timing):
        streams.append((label, source, destination))
        results[label] = (64, None)

    monkeypatch.setattr(stress, "configure_port", configure)
    monkeypatch.setattr(stress.threading, "Thread", ImmediateThread)
    monkeypatch.setattr(stress, "run_stream", run_stream)
    monkeypatch.setattr(stress, "close_ports", lambda *_: None)
    assert stress.benchmark_rate(arguments, 115200)
    expected = [("uart0-pico-to-peer", 1, 2), ("uart0-peer-to-pico", 2, 1),
                ("uart5-loopback", 5, 5)]
    if cross:
        expected.extend([("uart1-to-uart2", 6, 3), ("uart2-to-uart1", 3, 6),
                         ("uart3-to-uart4", 4, 7), ("uart4-to-uart3", 7, 4)])
    else:
        expected.extend([("uart2-to-uart3", 3, 4), ("uart3-to-uart2", 4, 3)])
        if uart1:
            expected.append(("uart1-loopback", 6, 6))
        if uart4:
            fd = 7 if uart1 else 6
            expected.append(("uart4-loopback", fd, fd))
    assert streams == expected
    assert len(opened) == len(set(opened)) == 5 + uart1 + uart4


def test_performance_plan_matches_modes_and_timing(repo_root):
    plan = (repo_root / "docs/tests/performance-test-plan.md").read_text()
    assert "seven concurrent verified" in plan
    assert "twelve simultaneous" not in plan
    assert "Optional `--uart1` and `--uart4` add independent" in plan
    assert "first-send-attempt" in plan
    assert "last in-flight block" in plan
