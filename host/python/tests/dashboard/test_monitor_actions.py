# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Hardware-free tests for monitor actions, diagnostics, reconnects, and registry reaping."""

import threading
import time

import pytest
from helpers import channel_bytes, configuration_bytes, device_info_bytes, diagnostic_bytes, telemetry_bytes

from pico_uart.dashboard.boards import MISSED_SCANS_BEFORE_REAP, BoardRegistry
from pico_uart.dashboard.monitor import BoardMonitor
from pico_uart.hid.inputs import InputTracker
from pico_uart.hid.protocol import (
    parse_channel,
    parse_configuration,
    parse_device_info,
    parse_diagnostic,
    parse_telemetry,
)


class StubClient:
    """Canned decoded values; the monitor only depends on the PicoUartHid read/action surface."""

    def __init__(self):
        self.calls = []
        self.led = True
        self.fail = None
        self.close_error = None
        self.block_toggle = threading.Event()
        self.toggle_entered = threading.Event()
        self.identity = parse_device_info(device_info_bytes())

    def _maybe_fail(self, name):
        self.calls.append(name)
        if self.fail == name:
            raise RuntimeError(f"{name} failed")

    def read_status(self, timeout_ms):
        time.sleep(0.005)

    def read_identity(self):
        self._maybe_fail("identity")
        return self.identity

    def read_telemetry(self):
        self._maybe_fail("telemetry")
        return parse_telemetry(telemetry_bytes(led=int(self.led)))

    def read_configuration(self):
        self._maybe_fail("configuration")
        return parse_configuration(configuration_bytes(), self.identity)

    def read_control(self):
        self._maybe_fail("control")
        return {"status": 0, "action": 0, "error_reason": 0, "error": "none"}

    def read_channel_diagnostics(self):
        self._maybe_fail("diagnostics")
        configuration = self.read_configuration()
        return [{"id": index, **parse_diagnostic(diagnostic_bytes(dropped=index), configuration)}
                for index in range(6)]

    def toggle_led(self):
        self.toggle_entered.set()
        self.block_toggle.wait(timeout=2)
        self.led = not self.led

    def reset_board(self):
        self.calls.append("reset")

    def save_configuration(self, profile, unsafe_overclock):
        self.calls.append(("save", profile, unsafe_overclock))
        return {"status": 2, "action": 4, "error_reason": 0, "error": "none"}

    def close(self):
        self.calls.append("close")
        if self.close_error:
            raise self.close_error


def _wait_for(predicate, timeout=2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.005)
    raise AssertionError("condition not reached")


@pytest.fixture
def monitor():
    client = StubClient()
    board_monitor = BoardMonitor(client_factory=lambda: client)
    _wait_for(lambda: board_monitor.snapshot()["identity"] is not None)
    yield board_monitor, client
    client.block_toggle.set()
    board_monitor.close()


def test_actions_reach_client_and_publish_results(monitor):
    board_monitor, client = monitor
    board_monitor.toggle_led()
    assert client.led is False and board_monitor.snapshot()["led"] == {"manual_on": False, "output_on": False}
    board_monitor.reset_board()
    board_monitor.save_configuration(1, True)
    assert "reset" in client.calls and ("save", 1, True) in client.calls
    assert board_monitor.snapshot()["configuration_save"]["status"] == 2


def test_slow_action_does_not_block_snapshot_reads(monitor):
    board_monitor, client = monitor
    worker = threading.Thread(target=board_monitor.toggle_led)
    worker.start()
    try:
        assert client.toggle_entered.wait(timeout=1)
        started = time.monotonic()
        assert board_monitor.snapshot()["connected"] is True
        assert time.monotonic() - started < 0.5
    finally:
        client.block_toggle.set()
        worker.join(timeout=2)


def test_diagnostics_success_and_failure_are_published(monitor):
    board_monitor, client = monitor
    snapshot = board_monitor.read_diagnostics()
    assert snapshot["overflow_counts"] == [item["rx_overrun_count"] for item in snapshot["channel_diagnostics"]]
    client.fail = "diagnostics"
    with pytest.raises(RuntimeError, match="diagnostics failed"):
        board_monitor.read_diagnostics()
    assert board_monitor.snapshot()["diagnostics_error"] == "diagnostics failed"


def test_telemetry_failure_keeps_identity_and_reports_error(monitor):
    board_monitor, client = monitor
    client.fail = "telemetry"
    _wait_for(lambda: board_monitor.snapshot()["runtime_error"] == "telemetry failed")
    snapshot = board_monitor.snapshot()
    assert snapshot["identity"] is not None and snapshot["runtime"] is None and snapshot["board"] is not None


def test_connection_failure_clears_device_state_and_actions_report_disconnected(monkeypatch):
    attempts = []

    def factory():
        attempts.append(1)
        raise RuntimeError("no board")

    board_monitor = BoardMonitor(client_factory=factory)
    try:
        _wait_for(lambda: board_monitor.snapshot()["error"] == "no board")
        snapshot = board_monitor.snapshot()
        assert snapshot["connected"] is False and snapshot["identity"] is None
        with pytest.raises(RuntimeError, match="no board"):
            board_monitor.toggle_led()
    finally:
        board_monitor.close()


def test_registry_reaps_boards_missing_for_several_scans():
    closed = []

    class Monitor:
        def __init__(self, serial_number, device_path):
            self.serial = serial_number

        def close(self):
            closed.append(self.serial)
            return True

    attached = [{"serial_number": "A", "path": b"/a"}]
    registry = BoardRegistry(discover=lambda: attached, monitor_factory=Monitor)
    board_id = registry.boards()["boards"][0]["id"]
    registry._monitor(board_id)
    attached.clear()
    for _ in range(MISSED_SCANS_BEFORE_REAP - 1):
        registry.boards()
        assert closed == []
    registry.boards()
    assert closed == ["A"]
    with pytest.raises(RuntimeError, match="unknown board"):
        registry._monitor(board_id)



def test_os_errors_are_sanitized_in_snapshot(monitor, caplog):
    board_monitor, client = monitor

    def fail():
        raise OSError("hidraw3: permission denied")

    client.read_channel_diagnostics = fail
    with pytest.raises(OSError, match="hidraw3"):
        board_monitor.read_diagnostics()
    assert board_monitor.snapshot()["diagnostics_error"] == "PicoUart HID I/O error"
    assert "hidraw3" in caplog.text


def test_transport_runtime_error_path_is_sanitized_in_dashboard(monitor, caplog):
    board_monitor, _client = monitor
    board_monitor._mark_disconnected(RuntimeError("failed to open PicoUart HID interface at /dev/hidraw3"))
    assert board_monitor.snapshot()["error"] == "PicoUart HID I/O error"
    assert "/dev/hidraw3" in caplog.text


def test_control_read_errors_are_published(monitor):
    board_monitor, client = monitor
    client.fail = "configuration"
    board_monitor._read_control(client)
    assert board_monitor.snapshot()["configuration_error"] == "configuration failed"

    client.fail = "control"
    board_monitor._read_control(client)
    assert board_monitor.snapshot()["configuration_save_error"] == "control failed"


def test_identity_read_error_is_published(monitor):
    board_monitor, client = monitor
    client.fail = "identity"
    board_monitor._read_static(client)
    snapshot = board_monitor.snapshot()
    assert snapshot["identity"] is None
    assert snapshot["hardware_error"] == "identity failed"


def test_client_close_os_error_is_sanitized(monitor, caplog):
    board_monitor, client = monitor
    client.close_error = OSError("hidraw3: close failed")
    assert board_monitor.close()
    snapshot = board_monitor.snapshot()
    assert snapshot["error"] == "failed to close PicoUart HID client"
    assert "hidraw3" in caplog.text


def test_reconnect_delay_backs_off_and_resets(monkeypatch):
    delays = []

    def factory():
        raise RuntimeError("no board")

    board_monitor = BoardMonitor(client_factory=factory)
    board_monitor.close()

    def record_wait(delay):
        delays.append(delay)
        if len(delays) == 6:
            board_monitor._stop.set()

    monkeypatch.setattr(board_monitor._stop, "wait", record_wait)
    board_monitor._stop.clear()
    board_monitor._poll()
    assert delays == [1.0, 2.0, 4.0, 8.0, 8.0, 8.0]


def test_reconnect_backoff_covers_read_failures_and_resets_after_success(monkeypatch):
    class ScriptedClient(StubClient):
        def __init__(self, results):
            super().__init__()
            self.results = iter(results)

        def read_status(self, timeout_ms):
            result = next(self.results)
            if isinstance(result, Exception):
                raise result
            return result

    class StopAfterRetries:
        def __init__(self):
            self.delays = []

        def is_set(self):
            return len(self.delays) >= 3

        def wait(self, delay):
            self.delays.append(delay)

    clients = iter([
        ScriptedClient([RuntimeError("first read failed")]),
        ScriptedClient([{"channels": []}, RuntimeError("read failed after recovery")]),
        ScriptedClient([RuntimeError("next read failed")]),
    ])
    delays = StopAfterRetries()
    monkeypatch.setattr(threading.Thread, "start", lambda _thread: None)
    board_monitor = BoardMonitor(client_factory=lambda: next(clients))
    board_monitor._stop = delays
    board_monitor._poll()

    assert delays.delays == [1.0, 1.0, 2.0]


def test_new_channel_sample_is_published_when_millisecond_timestamp_repeats(monitor):
    board_monitor, _client = monitor
    tracker = InputTracker()
    timestamp = "2026-11-11T19:00:00.000+00:00"

    for sequence in (1, 2):
        tracker.apply(
            0,
            parse_channel(channel_bytes(sequence=sequence, traffic=(sequence * 10, 0, 0, 0))),
            sequence / 2,
        )
        snapshot = tracker.snapshot()
        snapshot["channels"][0]["updated_at"] = timestamp
        board_monitor._apply_status(snapshot)

    channel = board_monitor.snapshot()["channels"][0]
    assert channel["updated_at"] == timestamp
    assert channel["sequence"] == 2
    assert channel["totals"]["uart_tx"] == 20


def test_monitor_and_registry_are_context_managers():
    client = StubClient()
    with BoardMonitor(client_factory=lambda: client) as board_monitor:
        _wait_for(lambda: board_monitor.snapshot()["connected"])
    assert not board_monitor._thread.is_alive()
    with BoardRegistry(monitor_factory=lambda **_: board_monitor) as registry:
        pass
    assert registry._closed
