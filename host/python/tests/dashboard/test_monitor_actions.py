# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Hardware-free tests for monitor actions, diagnostics, reconnects, and registry reaping."""

import threading
import time

import pytest
from helpers import configuration_bytes, device_info_bytes, diagnostic_bytes, telemetry_bytes

from pico_uart.dashboard.boards import MISSED_SCANS_BEFORE_REAP, BoardRegistry
from pico_uart.dashboard.monitor import BoardMonitor
from pico_uart.hid.protocol import (
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
        return parse_configuration(configuration_bytes(), self.identity)

    def read_control(self):
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

