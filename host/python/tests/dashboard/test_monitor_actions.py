# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Hardware-free tests for monitor actions, diagnostics, reconnects, and registry reaping."""

import threading
import time

import pytest
from helpers import channel_bytes, configuration_bytes, device_info_bytes, diagnostic_bytes, telemetry_bytes

from pico_uart.dashboard.boards import MISSED_SCANS_BEFORE_REAP, BoardRegistry
from pico_uart.dashboard.monitor import BoardMonitor, empty_snapshot
from pico_uart.hid.client import ControlOutcomeUnconfirmed
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


def test_old_telemetry_completion_cannot_overwrite_reconnected_snapshot(monitor):
    board_monitor, old_client = monitor
    assert board_monitor.close()
    entered, release = threading.Event(), threading.Event()
    with board_monitor._lock:
        board_monitor._client = old_client

    def delayed_telemetry():
        result = parse_telemetry(telemetry_bytes(uptime_ms=9999))
        entered.set()
        assert release.wait(timeout=2)
        return result

    old_client.read_telemetry = delayed_telemetry
    worker = threading.Thread(target=board_monitor._read_telemetry, args=(old_client,))
    worker.start()
    try:
        assert entered.wait(timeout=1)
        new_client = StubClient()
        with board_monitor._lock:
            board_monitor._client = new_client
            board_monitor._snapshot = empty_snapshot()
            board_monitor._snapshot["connected"] = True
            board_monitor._snapshot["identity"] = new_client.identity
            board_monitor._snapshot["runtime"] = parse_telemetry(
                telemetry_bytes(uptime_ms=10)
            )["runtime"]
        release.set()
        worker.join(timeout=1)
        assert not worker.is_alive()
        assert board_monitor.snapshot()["runtime"]["uptime_ms"] == 10
    finally:
        release.set()
        worker.join(timeout=1)


def test_old_telemetry_error_cannot_mark_reconnected_snapshot_failed(monitor):
    board_monitor, old_client = monitor
    assert board_monitor.close()
    entered, release = threading.Event(), threading.Event()
    with board_monitor._lock:
        board_monitor._client = old_client

    def delayed_failure():
        entered.set()
        assert release.wait(timeout=2)
        raise OSError("old connection disconnected")

    old_client.read_telemetry = delayed_failure
    worker = threading.Thread(target=board_monitor._read_telemetry, args=(old_client,))
    worker.start()
    try:
        assert entered.wait(timeout=1)
        new_client = StubClient()
        with board_monitor._lock:
            board_monitor._client = new_client
            board_monitor._snapshot = empty_snapshot()
            board_monitor._snapshot["connected"] = True
            board_monitor._snapshot["identity"] = new_client.identity
            board_monitor._snapshot["runtime"] = parse_telemetry(
                telemetry_bytes(uptime_ms=10)
            )["runtime"]
        release.set()
        worker.join(timeout=1)
        assert not worker.is_alive()
        snapshot = board_monitor.snapshot()
        assert snapshot["runtime"]["uptime_ms"] == 10
        assert snapshot["runtime_error"] is None
    finally:
        release.set()
        worker.join(timeout=1)


def test_old_diagnostics_completion_cannot_publish_into_reconnected_snapshot(monitor):
    board_monitor, old_client = monitor
    assert board_monitor.close()
    entered, release = threading.Event(), threading.Event()
    original = old_client.read_channel_diagnostics
    with board_monitor._lock:
        board_monitor._client = old_client

    def delayed_diagnostics():
        result = original()
        entered.set()
        assert release.wait(timeout=2)
        return result

    old_client.read_channel_diagnostics = delayed_diagnostics
    worker = threading.Thread(target=board_monitor.read_diagnostics)
    worker.start()
    try:
        assert entered.wait(timeout=1)
        with board_monitor._lock:
            board_monitor._client = StubClient()
            board_monitor._snapshot["channel_diagnostics"] = None
            board_monitor._snapshot["overflow_counts"] = [None] * 6
        release.set()
        worker.join(timeout=1)
        assert not worker.is_alive()
        assert board_monitor.snapshot()["channel_diagnostics"] is None
        assert board_monitor.snapshot()["overflow_counts"] == [None] * 6
    finally:
        release.set()
        worker.join(timeout=1)


def test_old_save_completion_cannot_replace_reconnected_control_result(monitor):
    board_monitor, old_client = monitor
    assert board_monitor.close()
    entered, release = threading.Event(), threading.Event()
    original = old_client.save_configuration
    with board_monitor._lock:
        board_monitor._client = old_client

    def delayed_save(profile, unsafe_overclock):
        result = original(profile, unsafe_overclock)
        entered.set()
        assert release.wait(timeout=2)
        return result

    old_client.save_configuration = delayed_save
    worker = threading.Thread(target=board_monitor.save_configuration, args=(0, False))
    worker.start()
    try:
        assert entered.wait(timeout=1)
        with board_monitor._lock:
            board_monitor._client = StubClient()
            board_monitor._snapshot["configuration_save"] = {
                "status": 0, "action": 0, "error_reason": 0, "error": "none"
            }
        release.set()
        worker.join(timeout=1)
        assert not worker.is_alive()
        assert board_monitor.snapshot()["configuration_save"]["action"] == 0
    finally:
        release.set()
        worker.join(timeout=1)


def test_unconfirmed_save_survives_old_control_results_until_a_new_save_is_confirmed(monitor):
    board_monitor, client = monitor
    assert board_monitor.close()

    def fail_save(*_args):
        raise ControlOutcomeUnconfirmed("disconnect after submission")

    client.save_configuration = fail_save
    with board_monitor._lock:
        board_monitor._client = client
    with pytest.raises(ControlOutcomeUnconfirmed):
        board_monitor.save_configuration(0, False)
    error = board_monitor.snapshot()["configuration_save_error"]
    assert "outcome is unconfirmed" in error
    assert board_monitor.snapshot()["configuration_save_unconfirmed"] is True

    class StopAfterConnectClient(StubClient):
        def read_status(self, timeout_ms):
            board_monitor._stop.set()

    reconnected = StopAfterConnectClient()
    board_monitor._client_factory = lambda: reconnected
    board_monitor._stop.clear()
    board_monitor._poll()
    assert board_monitor.snapshot()["configuration_save_error"] == error

    with board_monitor._lock:
        board_monitor._client = reconnected
        board_monitor._snapshot["identity"] = reconnected.identity

    reconnected.read_control = lambda: {
        "status": 5, "action": 4, "error_reason": 6, "error": "cdc_session_open"
    }
    board_monitor._read_control(reconnected)
    assert board_monitor.snapshot()["configuration_save_error"] == error
    assert board_monitor.snapshot()["configuration_save_unconfirmed"] is True
    assert board_monitor.snapshot()["configuration_save"] is None

    reconnected.read_control = lambda: {
        "status": 2, "action": 4, "error_reason": 0, "error": "none"
    }
    board_monitor._read_control(reconnected)
    assert board_monitor.snapshot()["configuration_save_error"] == error
    assert board_monitor.snapshot()["configuration_save_unconfirmed"] is True
    assert board_monitor.snapshot()["configuration_save"] is None

    board_monitor.save_configuration(0, False)
    snapshot = board_monitor.snapshot()
    assert snapshot["configuration_save_error"] is None
    assert snapshot["configuration_save_unconfirmed"] is False
    assert snapshot["configuration_save"]["status"] == 2


def test_stale_control_read_cannot_clear_a_new_unconfirmed_save(monitor):
    board_monitor, client = monitor
    assert board_monitor.close()
    with board_monitor._lock:
        board_monitor._client = client

    captured, publish = threading.Event(), threading.Event()
    stale_result = {"status": 5, "action": 4, "error_reason": 6, "error": "cdc_session_open"}

    def delayed_read_control():
        captured.set()
        assert publish.wait(timeout=2)
        return stale_result

    def fail_save(_profile, _unsafe_overclock):
        raise ControlOutcomeUnconfirmed("disconnect after submission")

    client.read_control = delayed_read_control
    client.save_configuration = fail_save
    worker = threading.Thread(target=board_monitor._read_control, args=(client,))
    worker.start()
    try:
        assert captured.wait(timeout=1)
        with pytest.raises(ControlOutcomeUnconfirmed):
            board_monitor.save_configuration(0, False)
        before = board_monitor.snapshot()
        assert before["configuration_save_error"] == (
            "board action outcome is unconfirmed; reconnect and inspect state before retrying"
        )
        assert before["configuration_save_unconfirmed"] is True
        publish.set()
        worker.join(timeout=1)
        assert not worker.is_alive()
        snapshot = board_monitor.snapshot()
        assert snapshot["configuration_save"] is None
        assert snapshot["configuration_save_error"] == (
            "board action outcome is unconfirmed; reconnect and inspect state before retrying"
        )
        assert snapshot["configuration_save_unconfirmed"] is True
    finally:
        publish.set()
        worker.join(timeout=1)


def test_control_read_started_during_save_cannot_clear_unconfirmed_outcome():
    class PausingLock:
        def __init__(self):
            self._lock = threading.RLock()
            self.pause_thread_id = None
            self.pause_next_exit = False
            self.transition_published = threading.Event()
            self.resume_saver = threading.Event()

        def __enter__(self):
            self._lock.acquire()
            return self

        def __exit__(self, *_args):
            self._lock.release()
            if (threading.get_ident() == self.pause_thread_id and self.pause_next_exit):
                self.pause_next_exit = False
                self.transition_published.set()
                assert self.resume_saver.wait(timeout=2)

    client = StubClient()
    board_monitor = BoardMonitor(client_factory=lambda: client)
    board_monitor.close()
    lock = PausingLock()
    board_monitor._lock = lock
    with lock:
        board_monitor._client = client
        board_monitor._snapshot["connected"] = True
        board_monitor._snapshot["identity"] = client.identity

    save_started = threading.Event()
    fail_save = threading.Event()
    old_result_captured = threading.Event()
    publish_old_result = threading.Event()
    stale_result = {"status": 5, "action": 4, "error_reason": 6, "error": "cdc_session_open"}

    def unconfirmed_save(_profile, _unsafe_overclock):
        save_started.set()
        assert fail_save.wait(timeout=2)
        lock.pause_thread_id = threading.get_ident()
        lock.pause_next_exit = True
        raise ControlOutcomeUnconfirmed("disconnect after submission")

    def delayed_control_read():
        old_result_captured.set()
        assert publish_old_result.wait(timeout=2)
        return stale_result

    client.save_configuration = unconfirmed_save
    client.read_control = delayed_control_read
    save_errors = []

    def save():
        try:
            board_monitor.save_configuration(0, False)
        except ControlOutcomeUnconfirmed as error:
            save_errors.append(str(error))

    saver = threading.Thread(target=save)
    reader = threading.Thread(target=board_monitor._read_control, args=(client,))
    try:
        saver.start()
        assert save_started.wait(timeout=1)
        reader.start()
        assert old_result_captured.wait(timeout=1)
        fail_save.set()
        assert lock.transition_published.wait(timeout=1)
        assert board_monitor.snapshot()["configuration_save_unconfirmed"] is True
        publish_old_result.set()
        reader.join(timeout=1)
        assert not reader.is_alive()
        lock.resume_saver.set()
        saver.join(timeout=1)
        assert not saver.is_alive()
        snapshot = board_monitor.snapshot()
        assert save_errors
        assert snapshot["configuration_save"] is None
        assert snapshot["configuration_save_unconfirmed"] is True
    finally:
        fail_save.set()
        publish_old_result.set()
        lock.resume_saver.set()
        saver.join(timeout=1)
        reader.join(timeout=1)


def test_led_action_remains_successful_when_telemetry_refresh_fails(monitor):
    board_monitor, client = monitor
    client.fail = "telemetry"

    board_monitor.toggle_led()

    snapshot = board_monitor.snapshot()
    assert client.led is False
    assert snapshot["led_error"] == "telemetry failed"


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
